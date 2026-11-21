#include "passes/raytracing/realtime/RestirResources.h"
#include "render/device/ResourceBarriers.h"

#include "core/Logger.h"

#include "passes/raytracing/realtime/RayTracingSettings.h"
#include "passes/raytracing/realtime/RestirFrameParameters.h"

#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/VulkanContext.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"

#include <bit>
#include <cstring>
#include <functional>
#include <string>
#include <tuple>

void RestirResources::compile(
    const ShaderLayout& shader,
    const std::array<VulkanResources::BufferHandle, kBindingCount>& buffers,
    ResourceBarriers& accesses
)
{
    constexpr auto kSet    = RenderInterface::kRayTracingPassSet;
    constexpr auto kStage  = vk::ShaderStageFlagBits::eRaygenKHR;
    const auto     storage = [&](std::string_view name)
    { return shader.binding<vk::DescriptorType::eStorageBuffer>(name, kSet).layout(kStage); };
    bindings = {
        storage("restirSurfaces"),
        storage("restirPreviousSurfaces"),
        storage("restirInitialReservoirs"),
        storage("restirHistory"),
        shader.uniformBuffer<RestirFrameParameters>("restirFrame", kSet).layout(kStage),
    };

    const auto& [surface, previousSurface, initial, history, frame] = buffers;
    surfaces          = {surface, previousSurface};
    initialReservoirs = initial;
    reservoirHistory  = history;
    parameters        = frame;

    constexpr VulkanResources::MemoryAccess kStorageReadWrite{
        kRayTracingStages,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
    };
    for (const auto surface : surfaces)
        accesses.add(surface, kStorageReadWrite);
    accesses.add(initialReservoirs, kStorageReadWrite);
    accesses.add(reservoirHistory, kStorageReadWrite);
    accesses.add(parameters, {kRayTracingStages, vk::AccessFlagBits2::eUniformRead});

    writeLayer = 0;
    hasHistory = false;
    frames     = {};
}

void RestirResources::prepare(
    VulkanResources&      cache,
    const Falcor::RenderContext&  frame,
    Falcor::uint2                 extent,
    const RestirSettings& settings,
    vk::DescriptorSet     set
)
{
    const auto&     data     = *frame.scene;
    const auto&     viewport = frame.view->viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const bool      ibl              = frame.config->feature.ibl && data.environmentMap.has_value();
    const auto      materialRevision = frame.materialTable->revision();
    uint64_t        signature        = std::hash<std::string>{}(data.sceneId.value);
    const auto      combine          = [&signature](uint64_t value)
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
    const RestirFrameParameters frameParameters{
        .previousViewProjection = previousViewProjection,
        .previousViewport       = previousViewport,
        .previousDepthRow       = previousDepthRow,
        .width                  = extent.x,
        .height                 = extent.y,
        .writeLayer             = writeLayer,
        .historyValid           = historyValid ? 1u : 0u,
        .maxHistoryLength       = settings.maxHistoryLength,
        .spatialSampleCount     = settings.spatialSampleCount,
        .spatialRadius          = settings.spatialRadius,
        .temporalReuse          = settings.temporalReuse ? 1u : 0u,
        .spatialReuse           = settings.spatialReuse ? 1u : 0u,
        .candidateCount         = settings.candidateCount,
    };
    const auto upload = cache.allocateUpload(parameters, sizeof(frameParameters));
    std::memcpy(upload.data.data(), &frameParameters, sizeof(frameParameters));

    const auto& view         = frame.view->uniforms.view;
    previousViewProjection   = frame.view->uniforms.viewProjection;
    previousViewport         = currentViewport;
    previousDepthRow         = -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
    previousSceneSignature   = signature;
    previousMaterialRevision = materialRevision;
    previousLights           = data.lights;
    previousEnvironment      = data.environmentMap;
    previousIbl              = ibl;

    updateDescriptors(cache, frame, set);
}

void RestirResources::updateDescriptors(VulkanResources& cache, const Falcor::RenderContext& frame, vk::DescriptorSet set)
{
    auto&            current = frames[frame.frameIndex];
    const std::array handles{
        surfaces[writeLayer],
        surfaces[writeLayer ^ 1u],
        initialReservoirs,
        reservoirHistory,
        parameters,
    };
    static_assert(std::tuple_size_v<decltype(handles)> == kBindingCount);
    std::array<vk::DescriptorBufferInfo, kBindingCount> infos;
    std::array<vk::WriteDescriptorSet, kBindingCount>   writes;
    uint32_t                                            writeCount = 0;
    for (uint32_t index = 0; index < handles.size(); ++index)
    {
        const auto revision     = cache.bufferRevision(handles[index]);
        const bool changedLayer = index < surfaces.size() && current.surfaceLayer != writeLayer;
        if (current.bufferRevisions[index] == revision && !changedLayer)
            continue;

        const auto& buffer             = cache.getBuffer(handles[index]);
        infos[index]                   = vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.getSize()};
        auto& write                    = writes[writeCount++];
        write                          = makeDescriptorWrite(set, bindings[index]);
        write.pBufferInfo              = &infos[index];
        current.bufferRevisions[index] = revision;
    }
    if (writeCount != 0)
        frame.vulkan->deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
    current.surfaceLayer = writeLayer;
}

void RestirResources::advanceHistory()
{
    writeLayer ^= 1u;
    hasHistory = true;
}
