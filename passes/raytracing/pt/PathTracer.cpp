#include "PathTracerSettings.h"
#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/raytracing/PathTracingShader.h"


#include <array>
#include <format>
#include <limits>

namespace
{
constexpr std::array kRaygenEntries{"raygenMain"};
constexpr char       kOutputSamples[] = "samples";

class PathTracer : public RenderPass
{
    RL_PLUGIN_CLASS(PathTracer, "PathTracer", "Reference path tracer.");

    PathTracer(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
        RenderPass(std::move(device), 1), parameters(deserializeFromProperties<PathTracerSettings>(properties))
    {
        parameters.validate();
    }

    [[nodiscard]] Properties getProperties() const override
    {
        return serializeToProperties(parameters);
    }

    void setProperties(const Properties& properties) override
    {
        PathTracerSettings next;
        Deserialize(nlohmann::json(properties.toJson()), next);
        next.validate();
        parameters = std::move(next);
    }

    static ref<PathTracer> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<PathTracer>(std::move(device), properties);
    }

    void renderUI(const PassEditorContext& context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData&) override
    {
        RenderPassReflection reflection;
        reflection.addOutput(kOutputSamples, rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::RGBA32Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        });
        return reflection;
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

        const auto environment = pathEnvironment(scene);
        auto& [pipeline, shaderTable] = programs[static_cast<size_t>(environment)];
        if (!pipeline)
        {
            auto&      shaders = *execution.shaderManager;
            auto*      program = shaders.getProgram(
                "passes/raytracing/pt/main.slang",
                {"raygenMain", "missMain", "visibilityMissMain", "closestHitMain"},
                pathTracingDefines(environment)
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
        const auto  output    = renderData.getTexture(kOutputSamples);
        const auto& size      = output->getDesc().size;
        const auto  constants = makePathTracingParameters(scene, parameters.maxBounces, sampleIndex);

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
    std::array<Program, static_cast<size_t>(PathEnvironment::Count)> programs;

    bool           sceneChanged       = true;
    uint32_t       sampleIndex        = 0;
    uint32_t       previousMaxBounces = 0;
};

bool drawSettings(
    Gui::Widgets& widgets, PathTracerSettings& settings, const PathTracer& pass, const PassEditorContext&
)
{
    widgets.text(std::format("Samples per pixel: {}", pass.sampleCount()));
    const bool edited = widgets.slider("Max bounces", settings.maxBounces, kMinPathBounces, kMaxPathBounces);
    widgets.tooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}
}  // namespace


void PathTracer::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context, "PT");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, PathTracer>();
}
