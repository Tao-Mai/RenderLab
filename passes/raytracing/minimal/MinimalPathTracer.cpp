#include "core/Logger.h"
#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/device/GpuProfiler.h"
#include "render/graph/PassPorts.h"
#include "render/raytracing/RealtimePathTracing.h"

namespace
{
constexpr std::array kRaygenEntries{"raygenMain"};
constexpr RealtimePathTracing::Program kProgram{
    .shaderPaths = {"shaders/minimal_path_tracer.spv", "shaders/minimal_path_tracer.svgf.spv"},
    .raygenEntries = kRaygenEntries,
    .extraBindings = {},
};

struct MinimalPathTracerSettings
{
    uint32_t maxBounces = 3;

    void validate() const { CHECK(maxBounces >= kMinPathBounces && maxBounces <= kMaxPathBounces); }
};

class MinimalPathTracer : public ConfigurablePass<MinimalPathTracerSettings, CompositeRenderPass>
{
public:
    [[nodiscard]] RenderPassReflection reflect(const PassSetup&) const override
    {
        RenderPassReflection reflection;
        reflection.input("lights", PortKind::Buffer);
        reflection.output("triangleLights", PortKind::Buffer);
        reflection.output("rayImage", PortKind::Image, true);
        pass_ports::outputs<DenoiserInputs>(reflection, true);
        return reflection;
    }

    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override
    {
        const auto geometry = graph.addStage("RT.Prepare emissive triangles", scene, setup);
        const auto output = graph.addStage("MinimalPathTracer", trace, setup, geometry, ports.buffer("lights"));

        ports.setOutput("triangleLights", geometry.triangles);
        ports.setOutput("rayImage", output.rayImage);
        pass_ports::write(ports, static_cast<const DenoiserInputs&>(output));
    }

    [[nodiscard]] nlohmann::json statistics() const override
    {
        return {{"emissiveTriangleCount", scene.emissiveTriangleCount()}};
    }

private:
    class TraceStage
    {
    public:
        explicit TraceStage(const MinimalPathTracerSettings& settings) : settings(settings) {}

        RealtimePathTracing::Output compile(
            RenderGraph& graph, const PassSetup& setup, RealtimeScene::Buffers geometry, RenderGraph::BufferHandle lights
        )
        {
            previousMaxBounces = settings.maxBounces;
            return tracer.compile(graph, setup, geometry, lights, kProgram);
        }

        void bindResources(RenderGraph& graph, const PassSetup& setup)
        {
            tracer.bindResources(graph, setup);
        }

        void prepare(RenderGraph& graph, const PassFrame& frame)
        {
            if (settings.maxBounces != previousMaxBounces)
            {
                tracer.invalidateDenoiserHistory(graph);
                previousMaxBounces = settings.maxBounces;
            }
            tracer.prepare(graph, frame);
        }

        void execute(RenderGraph&, vk::raii::CommandBuffer& commands, const PassFrame& frame)
        {
            tracer.bind(commands, frame, settings.maxBounces);
            tracer.dispatch(commands, 0);
        }

    private:
        const MinimalPathTracerSettings& settings;
        RealtimePathTracing tracer;
        uint32_t previousMaxBounces = 0;
    };

    RealtimeScene scene;
    TraceStage trace{parameters};
};

bool drawSettings(MinimalPathTracerSettings& settings, const MinimalPathTracer&, const PassEditorContext&)
{
    const bool edited = drawNumericSlider("Max bounces", settings.maxBounces, kMinPathBounces, kMaxPathBounces);
    ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<MinimalPathTracer>(makePassEditor<MinimalPathTracer, drawSettings>("MinimalPathTracer"));
}
