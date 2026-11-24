#include "passes/raytracing/restir/RayTracingPass.h"
#include "passes/raytracing/restir/RayTracingData.h"
#include "passes/raytracing/restir/RestirFrameParameters.h"


#include <slang-rhi/shader-cursor.h>
#include "core/Logger.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"
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


RayTracingPass::RayTracingPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device)), parameters(deserializeFromProperties<RayTracingSettings>(properties))
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
    reflection.addOutput("depth", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::R32Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    reflection.addOutput("worldNormal", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::RGBA16Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    reflection.addOutput(kOutputColor, rhi::TextureDesc{
        .type = parameters.splitLighting ? rhi::TextureType::Texture2DArray : rhi::TextureType::Texture2D,
        .size = {0, 0, 1},
        .arrayLength = parameters.splitLighting ? kLightingLayerCount : 1,
        .format = rhi::Format::RGBA16Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    if (parameters.splitLighting)
        for (const auto name : {"emission", "albedo"})
            reflection.addOutput(std::string{name}, rhi::TextureDesc{
                .size = {0, 0, 1},
                .format = rhi::Format::RGBA16Float,
                .usage = rhi::TextureUsage::UnorderedAccess,
            });
    reflection.addOutput("triangleLights", rhi::BufferDesc{
        .size = kLightTriangleHeaderSize + kLightTriangleStride,
        .usage = rhi::BufferUsage::ShaderResource,
    });

    const auto extent       = compileData.defaultTexDims;
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

    // Rebuild the table with the pipelines whose records it refers to.
    shaderTable.setNull();
    programs     = {};
}


void RayTracingPass::prepareTargets(RenderContext& context, const RenderData& renderData)
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
    previousSceneGeneration  = context.scene->sceneGeneration();
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
    auto* program = shaderManager.getOrCompile("raytracing", defines);
    const ShaderLayout layout(*program);
    layout.validateConstants<PathTracingParameters>("parameters");
    rhiCheck(mpDevice->createRayTracingPipeline(pathTracingPipelineDesc(program), variant.pipeline.writeRef()));
    if (!shaderTable)
        shaderTable = createPathTracingShaderTable(*mpDevice, program, kRaygenEntries);

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
    const auto rayTracing = frame.scene->prepareRayTracing(commands, frame.frameIndex);
    const bool  splitLighting  = parameters.splitLighting;
    const auto  environment    = pathEnvironment(*frame.scene);

    if (!targetsReady)
        prepareTargets(*renderCommands, renderData);
    auto& variant = programs[static_cast<size_t>(environment)];
    if (!variant.pipeline)
        prepareProgram(variant, frame, renderData, environment);

    renderData.setBuffer("triangleLights", rayTracing.triangleLightBuffer());
    const auto sceneGeneration = frame.scene->sceneGeneration();
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
    const bool  historyInvalidated =
        reservoirs.prepare(frame, renderData.getDefaultTextureDims(), parameters.restir, object);
    if (splitLighting && (optionsChanged || historyInvalidated))
        renderData.invalidateHistory(kOutputColor);
    rayTracing.bind(object);
    const auto constants = makePathTracingParameters(frame, parameters.maxBounces, sampleIndex++);
    rhiCheck(rhi::ShaderCursor(object)["parameters"].getDereferenced().setData(constants));
    const auto extent = renderData.getDefaultTextureDims();
    for (uint32_t stage = 0; stage < kRaygenEntries.size(); ++stage)
    {
        if (stage != 0)
            commands.globalBarrier();
        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        pass->bindPipeline(variant.pipeline, shaderTable, object);
        pass->dispatchRays(stage, extent.x, extent.y, 1);
        pass->end();
    }
    reservoirs.advanceHistory();

}
