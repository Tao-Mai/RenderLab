#include "PathTracerSettings.h"
#include "render/graph/RenderPass.h"

#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/offline/OfflinePathTracing.h"
#include "render/raytracing/offline/OfflineTracePipelineCache.h"

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
#include "test/PathTracingStats.h"
#endif

#include <format>

namespace
{
constexpr char kOutputColor[] = "color";

class PathTracer : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(PathTracer, "PathTracer", "Reference path tracer.");

    PathTracer(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
        Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<PathTracerSettings>(properties))
    {
        parameters.validate();
    }

    [[nodiscard]] Falcor::Properties getProperties() const override { return Falcor::serializeToProperties(parameters); }

    void setProperties(const Falcor::Properties& properties) override
    {
        PathTracerSettings next;
        Deserialize(nlohmann::json(properties.toJson()), next);
        next.validate();
        parameters = std::move(next);
    }

    static Falcor::ref<PathTracer> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<PathTracer>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData&) override
    {
        Falcor::RenderPassReflection reflection;
        reflection.addOutput(kOutputColor, "Path traced radiance")
            .format(Falcor::ResourceFormat::RGBA16Float)
            .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
        return reflection;
    }

    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override
    {
        tracer.compile(*passContext, compileData.defaultTexDims, "pt_resolve", sizeof(OfflinePathParameters));
        pipeline = passContext->offlinePipelines->get(*passContext->vulkan, tracer.layout(), "shaders/pt.spv", false);
    }

    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override
    {
        auto&       cache     = *renderCommands->vulkanResources;
        auto&       commands  = *renderCommands->commands;
        const auto& execution = *renderCommands;

        const bool sceneChanged = tracer.prepareScene(cache, execution, renderData.getTexture(kOutputColor));
        const bool optionsChanged = previousMaxBounces != parameters.maxBounces;
        if (optionsChanged)
        {
            auto& dictionary = renderData.getDictionary();
            const auto flags = dictionary.getValue(Falcor::kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
            dictionary[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
            previousMaxBounces = parameters.maxBounces;
        }
        if (sceneChanged || optionsChanged)
            tracer.resetAccumulation();

        cache.recordUploads(commands);
        const auto constants = tracer.pathParameters(execution, parameters.maxBounces);
        const auto bounds = tracer.viewportBounds(*execution.view);
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
            path_tracing_test::OfflineProgress{sampleCount(), float(sampleCount()), false};
#endif
    }

    [[nodiscard]] uint32_t sampleCount() const { return tracer.sampleCount(); }

private:
    PathTracerSettings parameters;

    OfflinePathTracing tracer;
    const RayTracingPipeline* pipeline = nullptr;
    uint32_t previousMaxBounces = 0;
};

bool drawSettings(Falcor::Gui::Widgets& widgets, PathTracerSettings& settings, const PathTracer& pass, const PassEditorContext&)
{
    widgets.text(std::format("Samples per pixel: {}", pass.sampleCount()));
    const bool edited = widgets.slider("Max bounces", settings.maxBounces, kMinPathBounces, kMaxPathBounces);
    widgets.tooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}
}

void PathTracer::renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context->passEditor, "PT");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, PathTracer>();
}
