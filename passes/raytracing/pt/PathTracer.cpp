#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/raytracing/offline/OfflinePathTracing.h"
#include "render/raytracing/offline/OfflineTracePipeline.h"

namespace
{
struct PathTracerSettings
{
    uint32_t maxBounces = 2;

    void validate() const { CHECK(maxBounces >= kMinPathBounces && maxBounces <= kMaxPathBounces); }
};

class PathTracer : public ConfigurablePass<PathTracerSettings>
{
public:
    [[nodiscard]] RenderPassReflection reflect(const PassSetup&) const override
    {
        RenderPassReflection reflection;
        reflection.output("rayImage", PortKind::Image);
        return reflection;
    }

    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override
    {
        const auto image = tracer.compile(graph, setup, "shaders/pt_resolve.spv", sizeof(OfflinePathParameters));
        pipeline = renderContext().offlinePipelines->get(setup.vulkan, tracer.layout(), "shaders/pt.spv", false);
        ports.setOutput("rayImage", image);
    }

    void bindResources(RenderGraph& graph, const PassSetup& setup) override { tracer.bindResources(graph, setup); }

    void prepare(RenderGraph& graph, const PassFrame& execution) override
    {
        if (tracer.prepareScene(graph, execution) || previousMaxBounces != parameters.maxBounces)
            tracer.resetAccumulation();
        previousMaxBounces = parameters.maxBounces;
    }

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& execution) override
    {
        const auto constants = tracer.pathParameters(execution, parameters.maxBounces);
        const auto bounds = tracer.viewportBounds(execution.view);
        const bool hasPixels = bounds.extent.width != 0 && bounds.extent.height != 0;
        tracer.render(
            graph, commands, execution, std::as_bytes(std::span{&constants, 1}),
            hasPixels ? pipeline : nullptr, {constants.imageSize.x, constants.imageSize.y}
        );
        if (hasPixels)
            tracer.finishSample();
    }

    [[nodiscard]] nlohmann::json statistics() const override
    {
        return {{"sampleCount", tracer.sampleCount()}, {"sampleProgress", tracer.sampleCount()},
                {"preparingPipeline", false}};
    }

    [[nodiscard]] uint32_t sampleCount() const { return tracer.sampleCount(); }

private:
    OfflinePathTracing tracer;
    const OfflineTracePipeline* pipeline = nullptr;
    uint32_t previousMaxBounces = 0;
};

bool drawSettings(PathTracerSettings& settings, const PathTracer& pass, const PassEditorContext&)
{
    ImGui::Text("Samples per pixel: %u", pass.sampleCount());
    const bool edited = drawNumericSlider("Max bounces", settings.maxBounces, kMinPathBounces, kMaxPathBounces);
    ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<PathTracer>(makePassEditor<PathTracer, drawSettings>("PT"));
}
