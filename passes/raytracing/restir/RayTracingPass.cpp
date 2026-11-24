#include "passes/raytracing/restir/RayTracingPass.h"
#include "passes/raytracing/restir/RayTracingData.h"
#include "passes/raytracing/restir/RestirFrameParameters.h"


#include <slang-rhi/shader-cursor.h>
#include "core/Logger.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"
#include "shaders/raytracing/LightingLayers.h"

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


RayTracingPass::RayTracingPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device), SceneInputs::RayTracing),
    parameters(deserializeFromProperties<RayTracingSettings>(properties))
{
    parameters.validate();
}


Properties RayTracingPass::getProperties() const { return serializeToProperties(parameters); }


void RayTracingPass::setProperties(const Properties& properties)
{
    RayTracingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.splitLighting != parameters.splitLighting;
    parameters           = std::move(next);
    if (recompile)
        requestRecompile();
}


RenderPassReflection RayTracingPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflection;
    reflection.addOutput("depth", "Scene depth")
        .format(rhi::Format::R32Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess);
    reflection.addOutput("worldNormal", "World-space normals")
        .format(rhi::Format::RGBA16Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess);
    reflection.addOutput(kOutputColor, "Path traced radiance")
        .texture2D(0, 0, 1, 1, parameters.splitLighting ? kLightingLayerCount : 1)
        .format(rhi::Format::RGBA16Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess);
    if (parameters.splitLighting)
        for (const auto name : {"emission", "albedo"})
            reflection.addOutput(std::string{name}, "Path tracing signal for denoising")
                .format(rhi::Format::RGBA16Float)
                .bindFlags(ResourceBindFlags::UnorderedAccess);
    reflection.addOutput("triangleLights", "World-space emissive triangles")
        .rawBuffer(kLightTriangleHeaderSize + kLightTriangleStride)
        .bindFlags(ResourceBindFlags::ShaderResource);

    const auto extent       = compileData.defaultTexDims;
    const auto pixels       = uint64_t{extent.x} * extent.y;
    const auto surfaceBytes = pixels * sizeof(RestirSurfaceData);
    const auto initialBytes = pixels * sizeof(RestirReservoirData);
    CHECK(std::in_range<uint32_t>(surfaceBytes) && std::in_range<uint32_t>(initialBytes * 2));

    for (const auto name : kSurfaces)
        reflection.addInternal(name, "ReSTIR surface records")
            .rawBuffer(static_cast<uint32_t>(surfaceBytes))
            .bindFlags(ResourceBindFlags::UnorderedAccess);
    reflection.addInternal(kInitialReservoirs, "Initial light reservoirs")
        .rawBuffer(static_cast<uint32_t>(initialBytes))
        .bindFlags(ResourceBindFlags::UnorderedAccess);

    reflection.addOutput(kHistory, "Temporal light reservoirs")
        .rawBuffer(static_cast<uint32_t>(initialBytes * 2))
        .bindFlags(ResourceBindFlags::UnorderedAccess);
    return reflection;
}


void RayTracingPass::compile(RenderContext*, const CompileData&)
{
    targetsReady = false;
    sampleIndex  = 0;
    programs     = {};
}


void RayTracingPass::prepareTargets(RenderContext& context, const RenderData& renderData)
{
    auto&      cache  = *context.gpuResources;
    const auto image  = [&](std::string_view name) { return cache.registerImage(renderData.getTexture(name)); };
    const auto buffer = [&](std::string_view name) { return cache.registerBuffer(renderData.getBuffer(name)); };
    reservoirs.compile({buffer(kSurfaces[0]), buffer(kSurfaces[1]), buffer(kInitialReservoirs), buffer(kHistory)});
    targetsReady             = true;
    historySource            = image("color");
    previousMaxBounces       = parameters.maxBounces;
    previousRestir           = parameters.restir;
    previousSceneGeneration = context.scene->sceneGeneration;
}

void RayTracingPass::prepareProgram(
    Program& variant, const RenderContext& context, const RenderData& renderData,
    PathEnvironment environment
)
{
    const bool splitLighting = parameters.splitLighting;
    auto&      shaderManager = *context.shaderManager;
    auto defines = pathEnvironmentDefines(environment);
    defines["SVGF"] = splitLighting ? "1" : "0";
    const auto shader = shaderManager.getOrCompile("raytracing", defines);
    const auto& layout = shaderManager.layout(shader);
    layout.validateConstants<PathTracingParameters>("parameters");
    auto* program = shaderManager.program(shader);
    variant.rayTracing.create(*mpDevice, program, kRaygenEntries);

    for (auto& object : variant.shaderObjects)
    {
        rhiCheck(mpDevice->createRootShaderObject(program, object.writeRef()));
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
}


void RayTracingPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    auto&       cache          = *renderCommands->gpuResources;
    auto&       commands       = *renderCommands->commands;
    const auto& frame          = *renderCommands;
    const bool  splitLighting  = parameters.splitLighting;
    const auto  environment    = pathEnvironment(*frame.scene);

    if (!targetsReady)
        prepareTargets(*renderCommands, renderData);
    auto& variant = programs[static_cast<size_t>(environment)];
    if (!variant.rayTracing.pipeline)
        prepareProgram(variant, frame, renderData, environment);

    renderData.setBuffer("triangleLights", frame.scene->rays.getTriangleBuffer());
    const auto sceneGeneration = frame.scene->sceneGeneration;
    const bool sceneChanged    = sceneGeneration != previousSceneGeneration;
    const bool optionsChanged  = parameters.maxBounces != previousMaxBounces || parameters.restir != previousRestir;
    if (sceneChanged)
        sampleIndex = 0;
    if (sceneChanged || optionsChanged)
        reservoirs.invalidateHistory();
    previousMaxBounces       = parameters.maxBounces;
    previousRestir           = parameters.restir;
    previousSceneGeneration = sceneGeneration;

    const auto& object = variant.shaderObjects[frame.frameIndex];
    const bool environmentReplaced =
        reservoirs.prepare(cache, frame, renderData.getDefaultTextureDims(), parameters.restir, object);
    if (splitLighting && (optionsChanged || environmentReplaced))
        cache.invalidateImageHistory(historySource);
    frame.scene->rays.bind(object, frame);
    const auto constants = makePathTracingParameters(frame, parameters.maxBounces, sampleIndex++);
    rhiCheck(rhi::ShaderCursor(object)["parameters"].getDereferenced().setData(constants));
    const auto extent = renderData.getDefaultTextureDims();
    for (uint32_t stage = 0; stage < kRaygenEntries.size(); ++stage)
    {
        if (stage != 0)
            commands.globalBarrier();
        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        pass->bindPipeline(variant.rayTracing.pipeline, variant.rayTracing.shaderTable, object);
        pass->dispatchRays(stage, extent.x, extent.y, 1);
        pass->end();
    }
    reservoirs.advanceHistory();

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
    renderData.getDictionary()[path_tracing_test::statsKey(getName())] =
        static_cast<uint32_t>(frame.scene->shading->lightTriangles.size());
#endif
}
