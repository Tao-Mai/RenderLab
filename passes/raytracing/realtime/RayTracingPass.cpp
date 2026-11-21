#include "passes/raytracing/realtime/RayTracingPass.h"

#include "core/Context.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/device/GpuProfiler.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"

#include <cmath>

namespace
{
constexpr std::array kRaygenEntries{"raygenMain", "temporalMain", "spatialMain"};
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
    RayTracingStorageImages::reflect(reflection);
    reflection.output("reservoirHistory", PortKind::Buffer);
    return reflection;
}

void RayTracingPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto geometry = graph.addStage("RT.Prepare emissive triangles", scene, setup);
    const auto output = graph.addStage("RT.Trace paths", trace, setup, geometry, ports.buffer("lights"));

    ports.setOutput("triangleLights", geometry.triangles);
    output.images.publish(ports);
    ports.setOutput("reservoirHistory", output.reservoirHistory);
}

nlohmann::json RayTracingPass::statistics() const
{
    return {{"emissiveTriangleCount", scene.emissiveTriangleCount()}};
}

RayTracingPass::TraceStage::Output RayTracingPass::TraceStage::compile(
    RenderGraph& graph, const PassSetup& setup, RealtimeScene::Buffers geometry, RenderGraph::BufferHandle lights
)
{
    splitLighting = setup.config.feature.svgf;
    auto& shaderManager = *renderContext().shaderManager;
    const auto shader = shaderManager.getOrLoad("raytracing", {{"SVGF", splitLighting ? 1 : 0}});
    const auto& layout = shaderManager.layout(shader);
    frameConstants = layout.pushConstants<PathTracingParameters>("parameters", vk::ShaderStageFlagBits::eRaygenKHR);

    sceneBindings.use(graph, layout, geometry, lights);
    const Output output{
        .images = images.compile(graph, setup, layout, splitLighting),
        .reservoirHistory = reservoirs.compile(graph, setup, layout),
    };

    auto& device = *renderContext().device;
    auto& variant = programs[splitLighting];
    if (!variant.pipeline)
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        sceneBindings.appendLayoutBindings(bindings);
        images.appendLayoutBindings(bindings);
        const auto& restirLayout = reservoirs.layoutBindings();
        bindings.insert(bindings.end(), restirLayout.begin(), restirLayout.end());
        variant.descriptorLayout = device.createDescriptorSetLayout(bindings);
        const auto pipelineLayout = RealtimeSceneBindings::getPipelineLayout(
            device, layout, variant.descriptorLayout, std::array{frameConstants.range}
        );
        variant.pipeline.emplace(setup.vulkan, RayTracingPipeline::Description{
            .shader = shaderManager.module(shader),
            .layout = pipelineLayout,
            .raygen = kRaygenEntries,
            .miss = kPathTracingMissEntries,
            .closestHit = kPathTracingHitEntries,
            .maxRecursionDepth = kRayPipelineRecursionDepth,
        });
    }
    descriptorSets.clear();
    descriptorSets.reserve(maxFramesInFlight);
    for (uint32_t index = 0; index < maxFramesInFlight; ++index)
        descriptorSets.push_back(device.allocateDescriptorSet(variant.descriptorLayout));

    historySource = output.images.denoiser.direct;
    previousMaxBounces = settings.maxBounces;
    previousRestir = settings.restir;
    sampleIndex = 0;
    return output;
}

void RayTracingPass::TraceStage::bindResources(RenderGraph& graph, const PassSetup& setup)
{
    images.writeDescriptors(graph, setup, descriptorSets);
}

void RayTracingPass::TraceStage::prepare(RenderGraph& graph, const PassFrame& frame)
{
    CpuProfileScope timing{context().profiler, "RT.Buffer preparation"};
    if (settings.maxBounces != previousMaxBounces || settings.restir != previousRestir)
    {
        reservoirs.invalidateHistory();
        if (splitLighting)
            graph.invalidateImageHistory(historySource);
        previousMaxBounces = settings.maxBounces;
        previousRestir = settings.restir;
    }

    const auto set = *descriptorSets[frame.frameIndex];
    sceneBindings.prepare(graph, frame, set);
    reservoirs.prepare(graph, frame, settings.restir, set);
}

void RayTracingPass::TraceStage::execute(RenderGraph&, vk::raii::CommandBuffer& commands, const PassFrame& frame)
{
    const auto& pipeline = *programs[splitLighting].pipeline;
    pipeline.bind(commands);
    sceneBindings.bind(commands, frame, pipeline.layout(), *descriptorSets[frame.frameIndex]);

    const auto parameters = makePathTracingParameters(frame, settings.maxBounces, sampleIndex++);
    frameConstants.push(commands, pipeline.layout(), parameters);

    const auto extent = frame.setup.renderExtent;
    const vk::Extent3D dispatchExtent{extent.width, extent.height, 1};
    constexpr std::array kStageNames{"RT.Initial RIS and paths", "ReSTIR.Temporal", "ReSTIR.Spatial and shading"};
    static_assert(kStageNames.size() == kRaygenEntries.size());
    for (uint32_t stage = 0; stage < kRaygenEntries.size(); ++stage)
    {
        if (stage != 0)
        {
            const vk::MemoryBarrier2 barrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
        }
        GpuProfileScope timing{frame.gpuProfiler, commands, kStageNames[stage]};
        pipeline.dispatch(commands, stage, dispatchExtent);
    }
    reservoirs.advanceHistory();
}
