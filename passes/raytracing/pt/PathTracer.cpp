#include "PathTracerSettings.h"
#include "render/graph/RenderPass.h"

#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/raytracing/PathAccumulation.h"
#include "render/raytracing/PathPipelineCache.h"

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
#include "test/PathTracingStats.h"
#endif

#include <format>

namespace
{
constexpr char kOutputColor[] = "color";

class PathTracer : public RenderPass
{
    RL_PLUGIN_CLASS(PathTracer, "PathTracer", "Reference path tracer.");

    PathTracer(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
        RenderPass(std::move(device), SceneInputs::RayTracing, 1),
        parameters(deserializeFromProperties<PathTracerSettings>(properties))
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

    void renderUI(RenderContext* context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData&) override
    {
        RenderPassReflection reflection;
        reflection.addOutput(kOutputColor, "Path traced radiance")
            .format(rhi::Format::RGBA16Float)
            .bindFlags(ResourceBindFlags::UnorderedAccess);
        return reflection;
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override
    {
        tracer.compile(*passContext, compileData.defaultTexDims, "pt_resolve");
        pipeline = nullptr;
    }

    void execute(RenderContext* renderCommands, const RenderData& renderData) override
    {
        auto&       cache     = *renderCommands->gpuResources;
        auto&       commands  = *renderCommands->commands;
        const auto& execution = *renderCommands;

        const auto environment = pathEnvironment(*execution.scene);
        if (!pipeline || environment != shaderEnvironment)
        {
            auto& shaders = *execution.shaderManager;
            const auto shader = shaders.getOrCompile("pt", pathEnvironmentDefines(environment));
            pipeline = execution.pathPipelines->get(*mpDevice, shaders, shader, false);
            shaderEnvironment = environment;
        }

        const bool sceneChanged   = tracer.updateFrame(execution, renderData.getTexture(kOutputColor));
        const bool optionsChanged = previousMaxBounces != parameters.maxBounces;
        if (sceneChanged || optionsChanged)
            tracer.resetAccumulation();
        previousMaxBounces = parameters.maxBounces;

        const auto constants = tracer.pathParameters(execution, parameters.maxBounces);
        const auto bounds    = tracer.viewportBounds(*execution.view);
        const bool hasPixels = bounds.extent.width != 0 && bounds.extent.height != 0;
        tracer.render(
            cache,
            commands,
            execution,
            std::as_bytes(std::span{&constants, 1}),
            hasPixels ? pipeline : nullptr,
            {constants.imageSize.x, constants.imageSize.y}
        );
        if (hasPixels)
            tracer.finishSample();

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
        renderData.getDictionary()[path_tracing_test::statsKey(getName())] =
            path_tracing_test::AccumulationProgress{sampleCount(), float(sampleCount()), false};
#endif
    }

    [[nodiscard]] uint32_t sampleCount() const { return tracer.sampleCount(); }

private:
    PathTracerSettings parameters;

    PathAccumulation          tracer;
    const PathTracingProgram* pipeline = nullptr;
    PathEnvironment           shaderEnvironment = PathEnvironment::None;
    uint32_t                  previousMaxBounces = 0;
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


void PathTracer::renderUI(RenderContext* context, Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context->passEditor, "PT");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, PathTracer>();
}
