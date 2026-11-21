#pragma once

#include "passes/raytracing/realtime/RayTracingSettings.h"
#include "passes/raytracing/realtime/RestirResources.h"

#include "render/graph/PassSettings.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/raytracing/RayTracingPipeline.h"
#include "render/raytracing/RayTracingStorageImages.h"
#include "render/raytracing/RealtimeSceneBindings.h"

#include <array>
#include <optional>
#include <vector>

class RayTracingPass : public ConfigurablePass<RayTracingSettings, CompositeRenderPass>
{
public:
    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    [[nodiscard]] nlohmann::json statistics() const override;

private:
    class TraceStage
    {
    public:
        explicit TraceStage(const RayTracingSettings& settings) : settings(settings) {}

        struct Output
        {
            RayTracingStorageImages::Outputs images;
            RenderGraph::BufferHandle reservoirHistory;
        };

        Output compile(
            RenderGraph& graph, const PassSetup& setup, RealtimeScene::Buffers scene, RenderGraph::BufferHandle lights
        );
        void bindResources(RenderGraph& graph, const PassSetup& setup);
        void prepare(RenderGraph& graph, const PassFrame& execution);
        void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& execution);

    private:
        struct Program
        {
            DescriptorSetLayout descriptorLayout;
            std::optional<RayTracingPipeline> pipeline;
        };

        const RayTracingSettings& settings;
        RealtimeSceneBindings sceneBindings;
        RayTracingStorageImages images;
        RestirResources reservoirs;
        std::array<Program, 2> programs;
        ShaderPushConstants<PathTracingParameters> frameConstants;
        std::vector<vk::raii::DescriptorSet> descriptorSets;
        RenderGraph::ImageHandle historySource;
        RestirSettings previousRestir;
        uint32_t sampleIndex = 0;
        uint32_t previousMaxBounces = 0;
        bool splitLighting = false;
    };

    RealtimeScene scene;
    TraceStage    trace{parameters};
};
