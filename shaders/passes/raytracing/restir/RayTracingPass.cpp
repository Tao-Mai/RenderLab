#include "shaders/passes/raytracing/restir/RayTracingPass.h"
#include "shaders/passes/raytracing/restir/RayTracingData.h"
#include "shaders/passes/raytracing/restir/RestirFrameParameters.h"


#include <slang-rhi/shader-cursor.h>
#include "core/Logger.h"
#include "core/Serializer.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"
#include "shaders/raytracing/LightingLayers.h"


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


RayTracingPass::RayTracingPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
    RenderPass(std::move(device)), parameters(DeserializeWithDefaults<RayTracingSettings>(properties))
{
    parameters.validate();
}

void RayTracingPass::onSettingsUpdated(const RayTracingSettings& previous)
{
    if (parameters.splitLighting != previous.splitLighting)
        requestRecompile();
}

RenderPassReflection RayTracingPass::reflect(const ResourceCache::DefaultProperties& defaults)
{
    const auto extent = defaults.dims;
    const rhi::Extent3D size{extent.x, extent.y, 1};
    RenderPassReflection reflection;
    reflection.addOutput("depth", rhi::TextureDesc{
        .size = size,
        .format = rhi::Format::R32Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    reflection.addOutput("worldNormal", rhi::TextureDesc{
        .size = size,
        .format = rhi::Format::RGBA16Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    reflection.addOutput(kOutputColor, rhi::TextureDesc{
        .type = parameters.splitLighting ? rhi::TextureType::Texture2DArray : rhi::TextureType::Texture2D,
        .size = size,
        .arrayLength = parameters.splitLighting ? kLightingLayerCount : 1,
        .format = rhi::Format::RGBA16Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    if (parameters.splitLighting)
        for (const auto name : {"emission", "albedo"})
            reflection.addOutput(std::string{name}, rhi::TextureDesc{
                .size = size,
                .format = rhi::Format::RGBA16Float,
                .usage = rhi::TextureUsage::UnorderedAccess,
            });

    const auto pixels       = uint64_t{extent.x} * extent.y;
    const auto surfaceBytes = pixels * sizeof(RestirSurfaceData);
    const auto initialBytes = pixels * sizeof(RestirReservoirData);
    CHECK(std::in_range<uint32_t>(surfaceBytes) && std::in_range<uint32_t>(initialBytes * 2));

    for (const auto name : kSurfaces)
        reflection.addInternal(name, rhi::BufferDesc{
            .size = static_cast<uint32_t>(surfaceBytes),
            .usage = rhi::BufferUsage::UnorderedAccess,
        });
    reflection.addInternal(kInitialReservoirs, rhi::BufferDesc{
        .size = static_cast<uint32_t>(initialBytes),
        .usage = rhi::BufferUsage::UnorderedAccess,
    });

    reflection.addOutput(kHistory, rhi::BufferDesc{
        .size = static_cast<uint32_t>(initialBytes * 2),
        .usage = rhi::BufferUsage::UnorderedAccess,
    });
    return reflection;
}


void RayTracingPass::compile(RenderContext*, const CompileData&)
{
    if (!checkRayTracingSupport(*mpDevice))
        return;

    targetsReady = false;
    sampleIndex  = 0;
    reservoirs   = {};

    programs = {};
}


void RayTracingPass::setScene(GpuScene* scene)
{
    RenderPass::setScene(scene);
    sceneUpdates |= SceneUpdateFlags::Scene;
}


void RayTracingPass::prepareTargets(const RenderData& renderData)
{
    reservoirs.compile({
        renderData.getBuffer(kSurfaces[0]),
        renderData.getBuffer(kSurfaces[1]),
        renderData.getBuffer(kInitialReservoirs),
        renderData.getBuffer(kHistory),
    });
    targetsReady             = true;
    previousMaxBounces       = parameters.maxBounces;
    previousRestir           = parameters.restir;
}

void RayTracingPass::prepareProgram(
    Program& variant, const RenderContext& context, const RenderData& renderData,
    bool hasEnvironment
)
{
    const bool splitLighting = parameters.splitLighting;
    auto&      shaderManager = *context.shaderManager;
    auto defines = pathTracingDefines(hasEnvironment);
    defines["SVGF"] = splitLighting ? "1" : "0";
    auto* program            = shaderManager.getProgram(
        "shaders/passes/raytracing/restir/restir_path_tracer.slang",
        {"raygenMain", "temporalMain", "spatialMain", "missMain", "visibilityMissMain", "closestHitMain"},
        defines,
        mpScene->materials().rayTracingShaderIncludes()
    );
    const ShaderLayout layout(*program);
    layout.validateConstants<PathTracingParameters>("parameters");
    rhiCheck(mpDevice->createRayTracingPipeline(pathTracingPipelineDesc(program), variant.pipeline.writeRef()));
    variant.shaderTable = createPathTracingShaderTable(*mpDevice, program, kRaygenEntries);

    for (auto& object : variant.shaderObjects)
    {
        rhiCheck(mpDevice->createRootShaderObject(program, object.writeRef()));
        const rhi::ShaderCursor root(object);
        const auto              bind = [&](const char* parameter, const char* field)
        {
            const auto texture = renderData.getTexture(field);
            CHECK(
                layout.storageImageFormat(parameter) == texture->getDesc().format,
                "storage image format differs for '{}'",
                parameter
            );
            rhiCheck(root[parameter].setBinding(texture));
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
    auto&       commands       = *renderCommands->commands;
    const auto& frame          = *renderCommands;
    auto&       scene          = *mpScene;
    auto*       tlas           = scene.prepareRayTracing(commands);
    scene.prepareEmissiveLights(commands);
    const bool  splitLighting  = parameters.splitLighting;
    const bool  hasEnvironment = scene.hasEnvironment();

    if (!targetsReady)
        prepareTargets(renderData);
    auto& variant = programs[hasEnvironment];
    if (!variant.pipeline)
        prepareProgram(variant, frame, renderData, hasEnvironment);

    const bool optionsChanged = parameters.maxBounces != previousMaxBounces || parameters.restir != previousRestir;
    const bool invalidateHistory = is_set(sceneUpdates, kSceneShadingUpdates) || optionsChanged;
    if (is_set(sceneUpdates, SceneUpdateFlags::Scene))
        sampleIndex = 0;
    if (invalidateHistory)
        reservoirs.invalidateHistory();
    previousMaxBounces       = parameters.maxBounces;
    previousRestir           = parameters.restir;
    sceneUpdates             = SceneUpdateFlags::None;

    const auto& object = variant.shaderObjects[frame.frameIndex];
    const auto size = renderData.getTexture(kOutputColor)->getDesc().size;
    reservoirs.prepare(scene, {size.width, size.height}, parameters.restir, object);
    if (splitLighting && invalidateHistory)
        renderData.invalidateHistory(kOutputColor);
    scene.bindRayTracingShaderData(*object, tlas);
    const PathTracingParameters constants{
        .maxBounces = parameters.maxBounces,
        .sampleIndex = sampleIndex++,
    };
    rhiCheck(rhi::ShaderCursor(object)["parameters"].getDereferenced().setData(constants));
    for (uint32_t stage = 0; stage < kRaygenEntries.size(); ++stage)
    {
        if (stage != 0)
            commands.globalBarrier();
        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        pass->bindPipeline(variant.pipeline, variant.shaderTable, object);
        pass->dispatchRays(stage, size.width, size.height, 1);
        pass->end();
    }
    reservoirs.advanceHistory();

}
