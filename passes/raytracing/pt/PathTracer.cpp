#include "PathTracerSettings.h"
#include "render/graph/RenderPassSettings.h"

#include <slang-rhi/shader-cursor.h>
#include "core/Serializer.h"
#include "render/GpuScene.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/raytracing/PathTracingShader.h"


#include <array>
#include <format>
#include <limits>

namespace
{
constexpr std::array kRaygenEntries{"raygenMain"};
constexpr char       kOutputSamples[] = "samples";

const RenderPassReflection::Field kPorts[]{
    {
        .name = kOutputSamples,
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::RGBA32Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        },
        .visibility = RenderPassReflection::Visibility::Output,
    },
};

class PathTracer : public RenderPass
{
public:
    PathTracer(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
        RenderPass(std::move(device), 1), parameters(DeserializeWithDefaults<PathTracerSettings>(properties))
    {
        parameters.validate();
    }

    static std::shared_ptr<PathTracer> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<PathTracer>(std::move(device), properties);
    }

    void compile(RenderContext*, const CompileData&) override
    {
        if (!checkRayTracingSupport(*mpDevice))
            return;

        sampleIndex = 0;
    }

    void setScene(GpuScene* scene) override
    {
        RenderPass::setScene(scene);
        sceneChanged = true;
    }

    void onSceneUpdates(RenderContext*, SceneUpdateFlags) override { sceneChanged = true; }

    void execute(RenderContext* renderCommands, const RenderData& renderData) override
    {
        const auto& execution = *renderCommands;
        auto&       scene     = *mpScene;
        auto&       commands  = *execution.commands;
        auto*       tlas      = scene.prepareRayTracing(commands);
        scene.prepareEmissiveLights(commands);

        const bool hasEnvironment = scene.hasEnvironment();
        auto& [pipeline, shaderTable] = programs[hasEnvironment];
        if (!pipeline)
        {
            auto&      shaders = *execution.shaderManager;
            auto*      program = shaders.getProgram(
                "passes/raytracing/pt/main.slang",
                {"raygenMain", "missMain", "visibilityMissMain", "closestHitMain"},
                pathTracingDefines(hasEnvironment)
            );
            rhiCheck(mpDevice->createRayTracingPipeline(pathTracingPipelineDesc(program), pipeline.writeRef()));
            shaderTable = createPathTracingShaderTable(*mpDevice, program, kRaygenEntries);
        }

        const bool optionsChanged = previousMaxBounces != parameters.maxBounces;
        if (sceneChanged || optionsChanged)
        {
            sampleIndex = 0;
            renderData.invalidateHistory(kOutputSamples);
        }
        sceneChanged = false;
        previousMaxBounces = parameters.maxBounces;

        CHECK(sampleIndex < std::numeric_limits<uint32_t>::max());
        const auto  output = renderData.getTexture(kOutputSamples);
        const auto& size   = output->getDesc().size;
        const PathTracingParameters constants{
            .maxBounces = parameters.maxBounces,
            .sampleIndex = sampleIndex,
        };

        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        auto* object = pass->bindPipeline(pipeline, shaderTable);
        CHECK(object);
        scene.bindRayTracingShaderData(*object, tlas);
        const rhi::ShaderCursor root(object);
        rhiCheck(root["sampleOutput"].setBinding(output));
        rhiCheck(root["parameters"].getDereferenced().setData(constants));
        pass->dispatchRays(0, size.width, size.height, 1);
        pass->end();

        const auto bounds = scene.viewport().pixelBounds({size.width, size.height});
        if (bounds.extent.width && bounds.extent.height)
            ++sampleIndex;

    }

    [[nodiscard]] uint32_t sampleCount() const { return sampleIndex; }

private:
    PathTracerSettings parameters;

    struct Program
    {
        rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
        rhi::ComPtr<rhi::IShaderTable> shaderTable;
    };
    std::array<Program, kPathEnvironmentVariantCount> programs;

    bool           sceneChanged       = true;
    uint32_t       sampleIndex        = 0;
    uint32_t       previousMaxBounces = 0;

    static bool drawSettings(
        Gui::Widgets& widgets, PathTracerSettings& settings, const PathTracer& pass, const PassEditorContext& context
    );

    RL_PLUGIN_CLASS(
        PathTracer,
        "PathTracer",
        .desc = "Reference path tracer.",
        .ports = kPorts,
        .settings = pass_settings::bind<&PathTracer::parameters, &PathTracer::drawSettings>()
    );
};

bool PathTracer::drawSettings(
    Gui::Widgets& widgets, PathTracerSettings& settings, const PathTracer& pass, const PassEditorContext&
)
{
    widgets.text(std::format("Samples per pixel: {}", pass.sampleCount()));
    const bool edited = widgets.slider("Max bounces", settings.maxBounces, kMinPathBounces, kMaxPathBounces);
    widgets.tooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}
}  // namespace


extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, PathTracer>();
}
