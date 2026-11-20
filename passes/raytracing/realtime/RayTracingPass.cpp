#include "passes/raytracing/realtime/RayTracingPass.h"
#include "passes/raytracing/realtime/RestirBindings.h"

#include "core/Context.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/GpuProfiler.h"
#include "render/device/VulkanContext.h"
#include "render/graph/PassPorts.h"
#include "render/resources/MaterialTable.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <functional>

namespace
{
constexpr std::array kRaygenEntries{"raygenMain", "temporalMain", "spatialMain"};
constexpr std::array kExtraBindings{
    vk::DescriptorSetLayoutBinding{RestirBinding::kSurfaces, vk::DescriptorType::eStorageBuffer, 1,
                                   vk::ShaderStageFlagBits::eRaygenKHR, nullptr},
    vk::DescriptorSetLayoutBinding{RestirBinding::kPreviousSurfaces, vk::DescriptorType::eStorageBuffer, 1,
                                   vk::ShaderStageFlagBits::eRaygenKHR, nullptr},
    vk::DescriptorSetLayoutBinding{RestirBinding::kInitialReservoirs, vk::DescriptorType::eStorageBuffer, 1,
                                   vk::ShaderStageFlagBits::eRaygenKHR, nullptr},
    vk::DescriptorSetLayoutBinding{RestirBinding::kReservoirHistory, vk::DescriptorType::eStorageBuffer, 1,
                                   vk::ShaderStageFlagBits::eRaygenKHR, nullptr},
    vk::DescriptorSetLayoutBinding{RestirBinding::kRestirFrame, vk::DescriptorType::eUniformBuffer, 1,
                                   vk::ShaderStageFlagBits::eRaygenKHR, nullptr},
};
constexpr RealtimePathTracing::Program kProgram{
    .shaderPaths = {"shaders/raytracing.spv", "shaders/raytracing.svgf.spv"},
    .raygenEntries = kRaygenEntries,
    .extraBindings = kExtraBindings,
};
}

void RayTracingSettings::validate() const
{
    CHECK(maxBounces >= kMinPathBounces && maxBounces <= kMaxPathBounces);
    CHECK(restir.candidateCount >= 1 && restir.candidateCount <= kMaxRestirCandidateCount);
    CHECK(restir.maxHistoryLength >= 1 && restir.maxHistoryLength <= kMaxRestirHistoryLength);
    CHECK(restir.spatialSampleCount >= 1 && restir.spatialSampleCount <= kMaxRestirSpatialSamples);
    CHECK(
        std::isfinite(restir.spatialRadius) && restir.spatialRadius >= 1.0f &&
        restir.spatialRadius <= kMaxRestirSpatialRadius
    );
}

RenderPassReflection RayTracingPass::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    reflection.input("lights", PortKind::Buffer);
    reflection.output("triangleLights", PortKind::Buffer);
    reflection.output("rayImage", PortKind::Image, true);
    reflection.output("reservoirHistory", PortKind::Buffer);
    pass_ports::outputs<DenoiserInputs>(reflection, true);
    return reflection;
}

void RayTracingPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto geometry = graph.addStage("RT.Prepare emissive triangles", scene, setup);
    const auto output   = graph.addStage("RT.Trace paths", trace, setup, geometry, ports.buffer("lights"));

    ports.setOutput("triangleLights", geometry.triangles);
    ports.setOutput("rayImage", output.path.rayImage);
    ports.setOutput("reservoirHistory", output.reservoirHistory);
    pass_ports::write(ports, static_cast<const DenoiserInputs&>(output.path));
}

nlohmann::json RayTracingPass::statistics() const
{
    return {{"emissiveTriangleCount", scene.emissiveTriangleCount()}};
}

RayTracingPass::TraceStage::Output RayTracingPass::TraceStage::compile(
    RenderGraph& graph, const PassSetup& setup, RealtimeScene::Buffers scene, RenderGraph::BufferHandle lights
)
{
    const auto path = tracer.compile(graph, setup, scene, lights, kProgram);
    extent = setup.renderExtent;

    const vk::DeviceSize pixelCount = vk::DeviceSize(extent.width) * extent.height;
    const auto createBuffer = [&](vk::DeviceSize size)
    {
        CHECK(
            size <= setup.vulkan.properties().limits.maxStorageBufferRange,
            "ReSTIR buffer requires {} bytes; GPU storage-buffer limit is {}",
            size,
            setup.vulkan.properties().limits.maxStorageBufferRange
        );
        const auto buffer = graph.write(graph.createBuffer({.size = size, .usage = vk::BufferUsageFlagBits::eStorageBuffer}));
        graph.use(
            buffer,
            {
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            }
        );
        return buffer;
    };
    for (auto& buffer : restirSurfaces)
        buffer = createBuffer(pixelCount * sizeof(RestirSurfaceData));
    initialReservoirs = createBuffer(pixelCount * sizeof(RestirReservoirData));
    reservoirHistory = createBuffer(pixelCount * sizeof(RestirReservoirData) * 2);
    restirParameters = graph.createBuffer({
        .size  = sizeof(RestirFrameParameters),
        .usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    graph.use(
        restirParameters,
        {
            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .access = vk::AccessFlagBits2::eUniformRead,
        }
    );

    writeLayer = 0;
    hasRestirHistory = false;
    frames = {};
    return {path, reservoirHistory};
}

void RayTracingPass::TraceStage::updateBufferDescriptors(RenderGraph& graph, const PassFrame& execution)
{
    auto& frame = frames[execution.frameIndex];
    const std::array handles{
        restirSurfaces[writeLayer],
        restirSurfaces[writeLayer ^ 1u],
        initialReservoirs,
        reservoirHistory,
        restirParameters
    };
    constexpr std::array kBindings{
        RestirBinding::kSurfaces,
        RestirBinding::kPreviousSurfaces,
        RestirBinding::kInitialReservoirs,
        RestirBinding::kReservoirHistory,
        RestirBinding::kRestirFrame
    };
    std::array<vk::DescriptorBufferInfo, kBindings.size()> infos;
    std::array<vk::WriteDescriptorSet, kBindings.size()>   writes;
    uint32_t                                writeCount = 0;
    for (uint32_t index = 0; index < handles.size(); ++index)
    {
        const auto revision     = graph.bufferRevision(handles[index]);
        const bool changedLayer = index < restirSurfaces.size() && frame.surfaceLayer != writeLayer;
        if (frame.bufferRevisions[index] == revision && !changedLayer)
        {
            continue;
        }
        const auto& buffer   = graph.getBuffer(handles[index]);
        infos[index]         = vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
        writes[writeCount++] = {
            .dstSet          = tracer.descriptorSet(execution.frameIndex),
            .dstBinding      = kBindings[index],
            .descriptorCount = 1,
            .descriptorType =
                index + 1 == handles.size() ? vk::DescriptorType::eUniformBuffer : vk::DescriptorType::eStorageBuffer,
            .pBufferInfo = &infos[index],
        };
        frame.bufferRevisions[index] = revision;
    }
    if (writeCount != 0)
        execution.setup.vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
    frame.surfaceLayer = writeLayer;
}

void RayTracingPass::TraceStage::prepare(RenderGraph& graph, const PassFrame& execution)
{
    CpuProfileScope timing{context().profiler, "RT.Buffer preparation"};
    if (settings.maxBounces != previousMaxBounces || settings.restir != previousRestir)
    {
        hasRestirHistory = false;
        tracer.invalidateDenoiserHistory(graph);
    }
    previousMaxBounces = settings.maxBounces;
    previousRestir = settings.restir;

    prepareRestir(graph, execution);
    tracer.prepare(graph, execution);
    updateBufferDescriptors(graph, execution);
}

void RayTracingPass::TraceStage::execute(RenderGraph&, vk::raii::CommandBuffer& commands, const PassFrame& execution)
{
    tracer.bind(commands, execution, settings.maxBounces);
    constexpr std::array kStageNames{"RT.Initial RIS and paths", "ReSTIR.Temporal", "ReSTIR.Spatial and shading"};
    static_assert(kStageNames.size() == kRaygenEntries.size());
    for (uint32_t stage = 0; stage < kRaygenEntries.size(); ++stage)
    {
        if (stage > 0)
        {
            const vk::MemoryBarrier2 barrier{
                .srcStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
        }
        GpuProfileScope timing{execution.gpuProfiler, commands, kStageNames[stage]};
        tracer.dispatch(commands, stage);
    }
    writeLayer ^= 1u;
    hasRestirHistory = true;
}

void RayTracingPass::TraceStage::prepareRestir(RenderGraph& graph, const PassFrame& execution)
{
    const auto&     data     = execution.renderData;
    const auto&     config   = execution.setup.config.feature;
    const auto&     viewport = execution.view.viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const bool      ibl              = config.ibl && data.environmentMap.has_value();
    const auto      materialRevision = renderContext().materialTable->revision();
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
        {
            for (uint32_t row = 0; row < 4; ++row)
            {
                combine(std::bit_cast<uint32_t>(item.model[column][row]));
            }
        }
    }
    const bool historyValid = hasRestirHistory && signature == previousSceneSignature &&
        materialRevision == previousMaterialRevision && data.lights == previousLights &&
        data.environmentMap == previousEnvironment && ibl == previousIbl;
    const RestirFrameParameters frameParameters{
        .previousViewProjection = previousViewProjection,
        .previousViewport       = previousViewport,
        .previousDepthRow       = previousDepthRow,
        .width                  = extent.width,
        .height                 = extent.height,
        .writeLayer             = writeLayer,
        .historyValid           = historyValid ? 1u : 0u,
        .maxHistoryLength       = settings.restir.maxHistoryLength,
        .spatialSampleCount     = settings.restir.spatialSampleCount,
        .spatialRadius          = settings.restir.spatialRadius,
        .temporalReuse          = settings.restir.temporalReuse ? 1u : 0u,
        .spatialReuse           = settings.restir.spatialReuse ? 1u : 0u,
        .candidateCount         = settings.restir.candidateCount,
    };
    const auto upload = graph.allocateUpload(restirParameters, sizeof(frameParameters));
    std::memcpy(upload.data.data(), &frameParameters, sizeof(frameParameters));

    const auto& view         = execution.view.uniforms.view;
    previousViewProjection   = execution.view.uniforms.viewProjection;
    previousViewport         = currentViewport;
    previousDepthRow         = -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
    previousSceneSignature   = signature;
    previousMaterialRevision = materialRevision;
    previousLights           = data.lights;
    previousEnvironment      = data.environmentMap;
    previousIbl              = ibl;
}
