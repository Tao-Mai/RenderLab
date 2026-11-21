#include "passes/raytracing/realtime/RayTracingPass.h"
#include "passes/raytracing/realtime/RayTracingData.h"
#include "passes/raytracing/realtime/RestirFrameParameters.h"
#include "render/device/ResourceBarriers.h"
#include "render/raytracing/RealtimeSceneData.h"

#include "core/profiling/Profiler.h"

#include "core/Context.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/denoise/DenoiserInputs.h"
#include "render/device/GpuProfiler.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/raytracing/RayTracingStorageImages.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
#include "test/PathTracingStats.h"
#endif

#include <cmath>
#include <utility>

namespace
{
constexpr char kOutputColor[] = "color";
constexpr std::array kSurfaces{"surface0", "surface1"};
constexpr char kInitialReservoirs[] = "initialReservoirs";
constexpr char kHistory[] = "reservoirHistory";
constexpr char kFrameParameters[] = "frameParameters";
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

RayTracingPass::RayTracingPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<RayTracingSettings>(properties))
{
    parameters.validate();
}

Falcor::Properties RayTracingPass::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void RayTracingPass::setProperties(const Falcor::Properties& properties)
{
    RayTracingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.splitLighting != parameters.splitLighting;
    parameters = std::move(next);
    if (recompile)
        requestRecompile();
}

Falcor::RenderPassReflection RayTracingPass::reflect(const CompileData& compileData)
{
    Falcor::RenderPassReflection reflection;
    reflection.addOutput("depth", "Scene depth").format(Falcor::ResourceFormat::R32Float).bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addOutput("worldNormal", "World-space normals")
        .format(Falcor::ResourceFormat::RGBA16Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addOutput(kOutputColor, "Path traced radiance")
        .texture2D(0, 0, 1, 1, parameters.splitLighting ? kLightingLayerCount : 1)
        .format(Falcor::ResourceFormat::RGBA16Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    if (parameters.splitLighting)
        for (const auto name : {"emission", "albedo"})
            reflection.addOutput(std::string{name}, "Path tracing signal for denoising")
                .format(Falcor::ResourceFormat::RGBA16Float)
                .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addOutput("triangleLights", "World-space emissive triangles")
        .rawBuffer(sizeof(EmissiveTriangleHeader) + sizeof(EmissiveTriangle))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);

    const auto extent = compileData.defaultTexDims;
    const auto pixels = uint64_t{extent.x} * extent.y;
    const auto surfaceBytes = pixels * sizeof(RestirSurfaceData);
    const auto initialBytes = pixels * sizeof(RestirReservoirData);
    CHECK(std::in_range<uint32_t>(surfaceBytes) && std::in_range<uint32_t>(initialBytes * 2));

    for (const auto name : kSurfaces)
        reflection.addInternal(name, "ReSTIR surface records")
            .rawBuffer(static_cast<uint32_t>(surfaceBytes))
            .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addInternal(kInitialReservoirs, "Initial light reservoirs")
        .rawBuffer(static_cast<uint32_t>(initialBytes))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addInternal(kFrameParameters, "ReSTIR frame parameters")
        .rawBuffer(sizeof(RestirFrameParameters))
        .bindFlags(Falcor::ResourceBindFlags::Constant);
    reflection.addOutput(kHistory, "Temporal light reservoirs")
        .rawBuffer(static_cast<uint32_t>(initialBytes * 2))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess)
        .flags(Falcor::RenderPassReflection::Field::Flags::Persistent);
    return reflection;
}

void RayTracingPass::compile(Falcor::RenderContext*, const CompileData&)
{
    barriers.clear();
    sampleIndex = 0;
}

void RayTracingPass::prepareTargets(Falcor::RenderContext& context, const Falcor::RenderData& renderData)
{
    auto&      cache  = *context.vulkanResources;
    const auto image  = [&](std::string_view name) { return cache.registerImage(renderData.getTexture(name)); };
    const auto buffer = [&](std::string_view name) { return cache.registerBuffer(renderData[name]->asBuffer()); };

    const auto     depth  = image("depth");
    const auto     normal = image("worldNormal");
    const auto     color  = image(kOutputColor);
    constexpr auto kRead  = vulkan_access::storageRead(kRayTracingStages);
    barriers.add(context.frameData->lightBufferHandle(), kRead);
    barriers.add(context.materialTable->bufferHandle(), kRead);
    barriers.add(depth, kRayTracingImageWrite);
    barriers.add(normal, kRayTracingImageWrite);
    barriers.add(color, kRayTracingImageWrite);

    const bool  splitLighting = parameters.splitLighting;
    auto&       shaderManager = *context.shaderManager;
    const auto  shader        = shaderManager.getOrLoad("raytracing", {{"SVGF", splitLighting ? "1" : "0"}});
    const auto& layout        = shaderManager.layout(shader);
    frameConstants = layout.pushConstants<PathTracingParameters>("parameters", vk::ShaderStageFlagBits::eRaygenKHR);

    const auto geometry = scene.compile(context, buffer("triangleLights"));
    traceAccesses.clear();
    sceneBindings.compile(traceAccesses, layout, geometry);
    const std::array reservoirBuffers{
        buffer(kSurfaces[0]),
        buffer(kSurfaces[1]),
        buffer(kInitialReservoirs),
        buffer(kHistory),
        buffer(kFrameParameters),
    };
    reservoirs.compile(layout, reservoirBuffers, traceAccesses);
    traceAccesses.declare(cache);
    RayTracingStorageImages images;

    images.add(cache, *context.vulkan, layout, "outputImage", color, kPathTracingColorFeatures);
    images.add(cache, *context.vulkan, layout, "depthImage", depth);
    images.add(cache, *context.vulkan, layout, "normalImage", normal);
    if (splitLighting)
    {
        constexpr std::array kSplitOutputs{
            std::pair{"emission", "emissionImage"},
            std::pair{"albedo", "albedoImage"},
        };
        for (const auto& [port, binding] : kSplitOutputs)
        {
            const auto target = image(port);
            barriers.add(target, kRayTracingImageWrite);
            images.add(cache, *context.vulkan, layout, binding, target);
        }
    }
    barriers.declare(cache);

    auto& device  = *mpDevice;
    auto& variant = programs[splitLighting];
    if (!variant.pipeline)
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        sceneBindings.appendLayoutBindings(bindings);
        images.appendLayoutBindings(bindings);
        const auto& restirLayout = reservoirs.layoutBindings();
        bindings.insert(bindings.end(), restirLayout.begin(), restirLayout.end());
        variant.descriptorLayout  = device.createDescriptorSetLayout(bindings);
        const auto pipelineLayout = RealtimeSceneBindings::getPipelineLayout(
            device,
            layout,
            variant.descriptorLayout,
            std::array{frameConstants.range}
        );
        variant.pipeline.emplace(
            *context.vulkan,
            RayTracingPipeline::Description{
                .shader            = shaderManager.module(shader),
                .layout            = pipelineLayout,
                .raygen            = kRaygenEntries,
                .miss              = kPathTracingMissEntries,
                .closestHit        = kPathTracingHitEntries,
                .maxRecursionDepth = kRayPipelineRecursionDepth,
            }
        );
    }
    descriptorSets.clear();
    descriptorSets.reserve(maxFramesInFlight);
    for (uint32_t index = 0; index < maxFramesInFlight; ++index)
        descriptorSets.push_back(device.allocateDescriptorSet(variant.descriptorLayout));

    images.writeDescriptors(cache, descriptorSets);

    historySource      = color;
    previousMaxBounces = parameters.maxBounces;
    previousRestir     = parameters.restir;
}

void RayTracingPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache         = *renderCommands->vulkanResources;
    auto&       commands      = *renderCommands->commands;
    const auto& frame         = *renderCommands;
    const bool  splitLighting = parameters.splitLighting;

    if (barriers.empty())
        prepareTargets(*renderCommands, renderData);
    barriers.transition(cache, commands);

    {
        CpuProfileScope timing{context().profiler, "RT.Buffer preparation"};
        scene.prepare(cache, frame);
        if (parameters.maxBounces != previousMaxBounces || parameters.restir != previousRestir)
        {
            auto& dictionary = renderData.getDictionary();
            const auto flags = dictionary.getValue(Falcor::kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
            dictionary[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;

            reservoirs.invalidateHistory();
            if (splitLighting)
                cache.invalidateImageHistory(historySource);
            previousMaxBounces = parameters.maxBounces;
            previousRestir     = parameters.restir;
        }

        const auto set = *descriptorSets[frame.frameIndex];
        sceneBindings.prepare(cache, frame, set);
        reservoirs.prepare(cache, frame, renderData.getDefaultTextureDims(), parameters.restir, set);
    }

    cache.recordUploads(commands);
    scene.execute(cache, commands, frame);
    traceAccesses.transition(cache, commands);

    const auto& pipeline = *programs[splitLighting].pipeline;
    pipeline.bind(commands);
    sceneBindings.bind(commands, frame, pipeline.layout(), *descriptorSets[frame.frameIndex]);

    const auto constants = makePathTracingParameters(frame, parameters.maxBounces, sampleIndex++);
    frameConstants.push(commands, pipeline.layout(), constants);

    const auto           extent = renderData.getDefaultTextureDims();
    const vk::Extent3D   dispatchExtent{extent.x, extent.y, 1};
    constexpr std::array kStageNames{"RT.Initial RIS and paths", "ReSTIR.Temporal", "ReSTIR.Spatial and shading"};
    static_assert(kStageNames.size() == kRaygenEntries.size());
    for (uint32_t stage = 0; stage < kRaygenEntries.size(); ++stage)
    {
        if (stage != 0)
        {
            const vk::MemoryBarrier2 barrier{
                .srcStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
        }
        GpuProfileScope timing{frame.gpuProfiler, commands, kStageNames[stage]};
        pipeline.dispatch(commands, stage, dispatchExtent);
    }
    reservoirs.advanceHistory();

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
    renderData.getDictionary()[path_tracing_test::statsKey(getName())] = scene.emissiveTriangleCount();
#endif
}
