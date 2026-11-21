#include "MinimalPathTracerSettings.h"

#include "core/Logger.h"
#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/raytracing/RayTracingPipeline.h"
#include "render/raytracing/RayTracingStorageImages.h"
#include "render/raytracing/RealtimeSceneBindings.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <optional>
#include <vector>

namespace
{
constexpr std::array kRaygenEntries{"raygenMain"};

class MinimalPathTracer : public ConfigurablePass<MinimalPathTracerSettings, CompositeRenderPass>
{
public:
    [[nodiscard]] RenderPassReflection reflect(const PassSetup&) const override
    {
        RenderPassReflection reflection;
        reflection.input("lights", PortKind::Buffer);
        reflection.output("triangleLights", PortKind::Buffer);
        RayTracingStorageImages::reflect(reflection);
        return reflection;
    }

    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override
    {
        const auto geometry = graph.addStage("RT.Prepare emissive triangles", scene, setup);
        const auto output = graph.addStage("MinimalPathTracer", trace, setup, geometry, ports.buffer("lights"));

        ports.setOutput("triangleLights", geometry.triangles);
        output.publish(ports);
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

        RayTracingStorageImages::Outputs compile(
            RenderGraph& graph, const PassSetup& setup, RealtimeScene::Buffers geometry, RenderGraph::BufferHandle lights
        )
        {
            splitLighting = setup.config.feature.svgf;
            auto& shaderManager = *renderContext().shaderManager;
            const auto shader = shaderManager.getOrLoad("minimal_path_tracer", {{"SVGF", splitLighting ? 1 : 0}});
            const auto& layout = shaderManager.layout(shader);
            frameConstants = layout.pushConstants<PathTracingParameters>("parameters", vk::ShaderStageFlagBits::eRaygenKHR);

            sceneBindings.use(graph, layout, geometry, lights);
            const auto output = images.compile(graph, setup, layout, splitLighting);

            auto& device = *renderContext().device;
            auto& variant = programs[splitLighting];
            if (!variant.pipeline)
            {
                std::vector<vk::DescriptorSetLayoutBinding> bindings;
                sceneBindings.appendLayoutBindings(bindings);
                images.appendLayoutBindings(bindings);
                variant.descriptorLayout = device.createDescriptorSetLayout(bindings);
                const auto pipelineLayout = RealtimeSceneBindings::getPipelineLayout(
                    device, layout, variant.descriptorLayout, std::array{frameConstants.range}
                );
                variant.pipeline.emplace(setup.vulkan, RayTracingPipeline::Description{
                    .shader = shaderManager.module(shader),
                    .layout = pipelineLayout,
                    .raygen = kRaygenEntries,
                    .miss = kPathTracingMissEntries,
                    .closestHit = kPathTracingHitEntries,
                    .maxRecursionDepth = kRayPipelineRecursionDepth,
                });
            }
            descriptorSets.clear();
            descriptorSets.reserve(maxFramesInFlight);
            for (uint32_t index = 0; index < maxFramesInFlight; ++index)
                descriptorSets.push_back(device.allocateDescriptorSet(variant.descriptorLayout));

            historySource = output.denoiser.direct;
            previousMaxBounces = settings.maxBounces;
            sampleIndex = 0;
            return output;
        }

        void bindResources(RenderGraph& graph, const PassSetup& setup)
        {
            images.writeDescriptors(graph, setup, descriptorSets);
        }

        void prepare(RenderGraph& graph, const PassFrame& frame)
        {
            if (settings.maxBounces != previousMaxBounces)
            {
                if (splitLighting)
                    graph.invalidateImageHistory(historySource);
                previousMaxBounces = settings.maxBounces;
            }
            sceneBindings.prepare(graph, frame, *descriptorSets[frame.frameIndex]);
        }

        void execute(RenderGraph&, vk::raii::CommandBuffer& commands, const PassFrame& frame)
        {
            const auto& pipeline = *programs[splitLighting].pipeline;
            pipeline.bind(commands);
            sceneBindings.bind(commands, frame, pipeline.layout(), *descriptorSets[frame.frameIndex]);

            const auto parameters = makePathTracingParameters(frame, settings.maxBounces, sampleIndex++);
            frameConstants.push(commands, pipeline.layout(), parameters);
            const auto extent = frame.setup.renderExtent;
            pipeline.dispatch(commands, 0, {extent.width, extent.height, 1});
        }

    private:
        struct Program
        {
            DescriptorSetLayout descriptorLayout;
            std::optional<RayTracingPipeline> pipeline;
        };

        const MinimalPathTracerSettings& settings;
        RealtimeSceneBindings sceneBindings;
        RayTracingStorageImages images;
        std::array<Program, 2> programs;
        ShaderPushConstants<PathTracingParameters> frameConstants;
        std::vector<vk::raii::DescriptorSet> descriptorSets;
        RenderGraph::ImageHandle historySource;
        uint32_t sampleIndex = 0;
        uint32_t previousMaxBounces = 0;
        bool splitLighting = false;
    };

    RealtimeScene scene;
    TraceStage trace{parameters};
};

bool drawSettings(Gui::Widgets& widgets, MinimalPathTracerSettings& settings, const MinimalPathTracer&,
                  const PassEditorContext&)
{
    const bool edited = widgets.slider("Max bounces", settings.maxBounces, kMinPathBounces, kMaxPathBounces);
    widgets.tooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<MinimalPathTracer>("MinimalPathTracer", makePassEditor<MinimalPathTracer, drawSettings>("MinimalPathTracer"));
}
