#include "passes/raytracing/realtime/RayTracingPass.h"
#include "passes/raytracing/realtime/RayTracingData.h"
#include "passes/raytracing/realtime/RestirFrameParameters.h"


#include "core/profiling/Profiler.h"

#include <slang-rhi/shader-cursor.h>
#include "core/Context.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/denoise/DenoiserInputs.h"
#include "render/device/GpuProfiler.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/raytracing/RealtimeSceneData.h"
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
constexpr char       kOutputColor[] = "color";
constexpr std::array kSurfaces{"surface0", "surface1"};
constexpr char       kInitialReservoirs[] = "initialReservoirs";
constexpr char       kHistory[]           = "reservoirHistory";
constexpr std::array kRaygenEntries{"raygenMain", "temporalMain", "spatialMain"};
}  // namespace


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


RayTracingPass::RayTracingPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<RayTracingSettings>(properties))
{
    parameters.validate();
}


Falcor::Properties RayTracingPass::getProperties() const { return Falcor::serializeToProperties(parameters); }


void RayTracingPass::setProperties(const Falcor::Properties& properties)
{
    RayTracingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.splitLighting != parameters.splitLighting;
    parameters           = std::move(next);
    if (recompile)
        requestRecompile();
}


Falcor::RenderPassReflection RayTracingPass::reflect(const CompileData& compileData)
{
    Falcor::RenderPassReflection reflection;
    reflection.addOutput("depth", "Scene depth")
        .format(rhi::Format::R32Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addOutput("worldNormal", "World-space normals")
        .format(rhi::Format::RGBA16Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addOutput(kOutputColor, "Path traced radiance")
        .texture2D(0, 0, 1, 1, parameters.splitLighting ? kLightingLayerCount : 1)
        .format(rhi::Format::RGBA16Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    if (parameters.splitLighting)
        for (const auto name : {"emission", "albedo"})
            reflection.addOutput(std::string{name}, "Path tracing signal for denoising")
                .format(rhi::Format::RGBA16Float)
                .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addOutput("triangleLights", "World-space emissive triangles")
        .rawBuffer(sizeof(EmissiveTriangleHeader) + sizeof(EmissiveTriangle))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);

    const auto extent       = compileData.defaultTexDims;
    const auto pixels       = uint64_t{extent.x} * extent.y;
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

    reflection.addOutput(kHistory, "Temporal light reservoirs")
        .rawBuffer(static_cast<uint32_t>(initialBytes * 2))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    return reflection;
}


void RayTracingPass::compile(Falcor::RenderContext*, const CompileData&)
{
    targetsReady = false;
    sampleIndex  = 0;
}


void RayTracingPass::prepareTargets(Falcor::RenderContext& context, const Falcor::RenderData& renderData)
{
    auto&       cache         = *context.gpuResources;
    const auto  image         = [&](std::string_view name) { return cache.registerImage(renderData.getTexture(name)); };
    const auto  buffer        = [&](std::string_view name) { return cache.registerBuffer(renderData.getBuffer(name)); };
    const bool  splitLighting = parameters.splitLighting;
    auto&       shaderManager = *context.shaderManager;
    const auto  shader        = shaderManager.getOrLoad("raytracing", {{"SVGF", splitLighting ? "1" : "0"}});
    const auto& layout        = shaderManager.layout(shader);

    layout.validateConstants<PathTracingParameters>("parameters");
    scene.compile(context, buffer("triangleLights"));
    reservoirs.compile({buffer(kSurfaces[0]), buffer(kSurfaces[1]), buffer(kInitialReservoirs), buffer(kHistory)});
    auto& variant = programs[splitLighting];
    if (!variant.pipeline)
    {

        const std::array groups{
            rhi::HitGroupDesc{"hitGroup0", "closestHitMain"},
            rhi::HitGroupDesc{"hitGroup1", "lightClosestHitMain"}
        };
        constexpr std::array        kGroupNames{"hitGroup0", "hitGroup1"};
        rhi::RayTracingPipelineDesc desc;
        desc.program           = shaderManager.program(shader);
        desc.hitGroups         = groups.data();
        desc.hitGroupCount     = uint32_t(groups.size());
        desc.maxRecursion      = kRayPipelineRecursionDepth;
        desc.maxRayPayloadSize = kPathTracingPayloadBytes;
        rhiCheck(mpDevice->createRayTracingPipeline(desc, variant.pipeline.writeRef()));
        rhi::ShaderTableDesc tableDesc;
        tableDesc.program                     = desc.program;
        tableDesc.rayGenShaderEntryPointNames = kRaygenEntries.data();
        tableDesc.rayGenShaderCount           = uint32_t(kRaygenEntries.size());
        tableDesc.missShaderEntryPointNames   = kPathTracingMissEntries.data();
        tableDesc.missShaderCount             = uint32_t(kPathTracingMissEntries.size());
        tableDesc.hitGroupNames               = kGroupNames.data();
        tableDesc.hitGroupCount               = uint32_t(kGroupNames.size());
        rhiCheck(mpDevice->createShaderTable(tableDesc, variant.shaderTable.writeRef()));
    }
    shaderObjectsByFrame.assign(kMaxFramesInFlight, {});
    for (auto& object : shaderObjectsByFrame)
    {
        rhiCheck(mpDevice->createRootShaderObject(shaderManager.program(shader), object.writeRef()));
        const rhi::ShaderCursor root(object);
        const auto              bind = [&](const char* parameter, const char* field)
        {
            const auto texture = renderData.getTexture(field);
            CHECK(
                layout.storageImage(parameter).format == texture->getDesc().format,
                "storage image format differs for '{}'",
                parameter
            );
            rhiCheck(root[parameter].setBinding(texture->getDefaultView()));
        };
        bind("outputImage", "color");
        bind("depthImage", "depth");
        bind("normalImage", "worldNormal");
        if (splitLighting)
        {
            bind("emissionImage", "emission");
            bind("albedoImage", "albedo");
        }
    }
    targetsReady       = true;
    historySource      = image("color");
    previousMaxBounces = parameters.maxBounces;
    previousRestir     = parameters.restir;
}


void RayTracingPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache         = *renderCommands->gpuResources;
    auto&       commands      = *renderCommands->commands;
    const auto& frame         = *renderCommands;
    const bool  splitLighting = parameters.splitLighting;

    if (!targetsReady)
        prepareTargets(*renderCommands, renderData);

    {
        CpuProfileScope timing{context().profiler, "RT.Buffer preparation"};
        renderData.setBuffer("triangleLights", scene.prepare(cache, frame));
        if (parameters.maxBounces != previousMaxBounces || parameters.restir != previousRestir)
        {
            auto&      dictionary = renderData.getDictionary();
            const auto flags =
                dictionary.getValue(Falcor::kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
            dictionary[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;

            reservoirs.invalidateHistory();
            if (splitLighting)
                cache.invalidateImageHistory(historySource);
            previousMaxBounces = parameters.maxBounces;
            previousRestir     = parameters.restir;
        }

        reservoirs.prepare(
            cache,
            frame,
            renderData.getDefaultTextureDims(),
            parameters.restir,
            shaderObjectsByFrame[frame.frameIndex]
        );
    }

    cache.recordUploads(commands);
    scene.execute(commands, frame);


    const auto& program = programs[splitLighting];
    const auto& object  = shaderObjectsByFrame[frame.frameIndex];
    scene.bind(object, cache, frame);
    const auto constants = makePathTracingParameters(frame, parameters.maxBounces, sampleIndex++);
    rhiCheck(rhi::ShaderCursor(object)["parameters"].getDereferenced().setData(constants));
    const auto           extent = renderData.getDefaultTextureDims();
    constexpr std::array kStageNames{"RT.Initial RIS and paths", "ReSTIR.Temporal", "ReSTIR.Spatial and shading"};
    static_assert(kStageNames.size() == kRaygenEntries.size());
    for (uint32_t stage = 0; stage < kRaygenEntries.size(); ++stage)
    {
        if (stage != 0)
            commands.globalBarrier();
        GpuProfileScope timing{frame.gpuProfiler, commands, kStageNames[stage]};
        auto*           pass = commands.beginRayTracingPass();
        CHECK(pass);
        pass->bindPipeline(program.pipeline, program.shaderTable, object);
        pass->dispatchRays(stage, extent.x, extent.y, 1);
        pass->end();
    }
    reservoirs.advanceHistory();

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
    renderData.getDictionary()[path_tracing_test::statsKey(getName())] = scene.emissiveTriangleCount();
#endif
}
