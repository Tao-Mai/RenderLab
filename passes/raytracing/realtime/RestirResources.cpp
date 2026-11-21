#include "passes/raytracing/realtime/RestirResources.h"

#include "passes/raytracing/realtime/RayTracingData.h"
#include "passes/raytracing/realtime/RayTracingSettings.h"
#include "passes/raytracing/realtime/RestirFrameParameters.h"

#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/VulkanContext.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"

#include <bit>
#include <cstring>
#include <functional>
#include <string>
#include <tuple>

RenderGraph::BufferHandle RestirResources::compile(RenderGraph& graph, const PassSetup& setup, const ShaderLayout& shader)
{
    constexpr auto kSet = RenderInterface::kRayTracingPassSet;
    constexpr auto kStage = vk::ShaderStageFlagBits::eRaygenKHR;
    const auto storage = [&](std::string_view name)
    {
        return shader.binding<vk::DescriptorType::eStorageBuffer>(name, kSet).layout(kStage);
    };
    bindings = {
        storage("restirSurfaces"),
        storage("restirPreviousSurfaces"),
        storage("restirInitialReservoirs"),
        storage("restirHistory"),
        shader.uniformBuffer<RestirFrameParameters>("restirFrame", kSet).layout(kStage),
    };

    const auto extent = setup.renderExtent;
    const vk::DeviceSize pixelCount = vk::DeviceSize(extent.width) * extent.height;
    const auto createBuffer = [&](vk::DeviceSize size)
    {
        const auto limit = setup.vulkan.properties().limits.maxStorageBufferRange;
        CHECK(size <= limit, "ReSTIR buffer requires {} bytes; GPU storage-buffer limit is {}", size, limit);
        const auto buffer = graph.write(graph.createBuffer({.size = size, .usage = vk::BufferUsageFlagBits::eStorageBuffer}));
        graph.use(buffer, {
            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        });
        return buffer;
    };
    for (auto& surface : surfaces)
        surface = createBuffer(pixelCount * sizeof(RestirSurfaceData));
    initialReservoirs = createBuffer(pixelCount * sizeof(RestirReservoirData));
    reservoirHistory = createBuffer(pixelCount * sizeof(RestirReservoirData) * surfaces.size());
    parameters = graph.createBuffer({
        .size = sizeof(RestirFrameParameters),
        .usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    graph.use(parameters, {
        .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        .access = vk::AccessFlagBits2::eUniformRead,
    });

    writeLayer = 0;
    hasHistory = false;
    frames = {};
    return reservoirHistory;
}

void RestirResources::prepare(
    RenderGraph& graph, const PassFrame& frame, const RestirSettings& settings, vk::DescriptorSet set
)
{
    const auto& data = frame.renderData;
    const auto& viewport = frame.view.viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const bool ibl = frame.setup.config.feature.ibl && data.environmentMap.has_value();
    const auto materialRevision = renderContext().materialTable->revision();
    uint64_t signature = std::hash<std::string>{}(data.sceneId.value);
    const auto combine = [&signature](uint64_t value)
    {
        constexpr uint64_t kHashGoldenRatio = 0x9e3779b97f4a7c15ull;
        signature ^= value + kHashGoldenRatio + (signature << 6) + (signature >> 2);
    };
    for (uint32_t index = 0; index < data.drawItems.size(); ++index)
    {
        const auto& item = data.drawItems[index];
        combine(std::hash<std::string>{}(item.mesh.value));
        combine(item.submeshIndex);
        combine(item.selectionId);
        combine(data.materialIndices[index]);
        for (uint32_t column = 0; column < 4; ++column)
            for (uint32_t row = 0; row < 4; ++row)
                combine(std::bit_cast<uint32_t>(item.model[column][row]));
    }

    const bool historyValid = hasHistory && signature == previousSceneSignature &&
        materialRevision == previousMaterialRevision && data.lights == previousLights &&
        data.environmentMap == previousEnvironment && ibl == previousIbl;
    const auto extent = frame.setup.renderExtent;
    const RestirFrameParameters frameParameters{
        .previousViewProjection = previousViewProjection,
        .previousViewport = previousViewport,
        .previousDepthRow = previousDepthRow,
        .width = extent.width,
        .height = extent.height,
        .writeLayer = writeLayer,
        .historyValid = historyValid ? 1u : 0u,
        .maxHistoryLength = settings.maxHistoryLength,
        .spatialSampleCount = settings.spatialSampleCount,
        .spatialRadius = settings.spatialRadius,
        .temporalReuse = settings.temporalReuse ? 1u : 0u,
        .spatialReuse = settings.spatialReuse ? 1u : 0u,
        .candidateCount = settings.candidateCount,
    };
    const auto upload = graph.allocateUpload(parameters, sizeof(frameParameters));
    std::memcpy(upload.data.data(), &frameParameters, sizeof(frameParameters));

    const auto& view = frame.view.uniforms.view;
    previousViewProjection = frame.view.uniforms.viewProjection;
    previousViewport = currentViewport;
    previousDepthRow = -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
    previousSceneSignature = signature;
    previousMaterialRevision = materialRevision;
    previousLights = data.lights;
    previousEnvironment = data.environmentMap;
    previousIbl = ibl;

    updateDescriptors(graph, frame, set);
}

void RestirResources::updateDescriptors(RenderGraph& graph, const PassFrame& frame, vk::DescriptorSet set)
{
    auto& current = frames[frame.frameIndex];
    const std::array handles{
        surfaces[writeLayer], surfaces[writeLayer ^ 1u], initialReservoirs, reservoirHistory, parameters,
    };
    static_assert(std::tuple_size_v<decltype(handles)> == kBindingCount);
    std::array<vk::DescriptorBufferInfo, kBindingCount> infos;
    std::array<vk::WriteDescriptorSet, kBindingCount> writes;
    uint32_t writeCount = 0;
    for (uint32_t index = 0; index < handles.size(); ++index)
    {
        const auto revision = graph.bufferRevision(handles[index]);
        const bool changedLayer = index < surfaces.size() && current.surfaceLayer != writeLayer;
        if (current.bufferRevisions[index] == revision && !changedLayer)
            continue;

        const auto& buffer = graph.getBuffer(handles[index]);
        infos[index] = vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
        auto& write = writes[writeCount++];
        write = makeDescriptorWrite(set, bindings[index]);
        write.pBufferInfo = &infos[index];
        current.bufferRevisions[index] = revision;
    }
    if (writeCount != 0)
        frame.setup.vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
    current.surfaceLayer = writeLayer;
}

void RestirResources::advanceHistory()
{
    writeLayer ^= 1u;
    hasHistory = true;
}
