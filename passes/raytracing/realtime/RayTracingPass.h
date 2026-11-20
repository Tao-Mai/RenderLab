#pragma once

#include "passes/raytracing/realtime/RayTracingData.h"
#include "passes/raytracing/realtime/RayTracingSettings.h"

#include "asset/EnvironmentMapAsset.h"
#include "render/graph/PassSettings.h"
#include "render/raytracing/RealtimePathTracing.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <glm/vec4.hpp>

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
            RealtimePathTracing::Output path;
            RenderGraph::BufferHandle reservoirHistory;
        };

        Output compile(
            RenderGraph& graph, const PassSetup& setup, RealtimeScene::Buffers scene, RenderGraph::BufferHandle lights
        );
        void bindResources(RenderGraph& graph, const PassSetup& setup)
        {
            tracer.bindResources(graph, setup);
        }
        void prepare(RenderGraph& graph, const PassFrame& execution);
        void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& execution);

    private:
        struct Frame
        {
            std::array<uint64_t, 5> bufferRevisions{};
            uint32_t surfaceLayer = ~0u;
        };

        const RayTracingSettings& settings;
        RealtimePathTracing tracer;
        std::array<Frame, maxFramesInFlight> frames;
        std::array<RenderGraph::BufferHandle, 2> restirSurfaces;
        RenderGraph::BufferHandle initialReservoirs;
        RenderGraph::BufferHandle reservoirHistory;
        RenderGraph::BufferHandle restirParameters;
        vk::Extent2D                                   extent;
        uint32_t                                       writeLayer = 0;
        bool                                           hasRestirHistory = false;
        glm::mat4                                      previousViewProjection{1.0f};
        glm::vec4                                      previousViewport{};
        glm::vec4                                      previousDepthRow{};
        uint64_t                                       previousSceneSignature = 0;
        uint64_t                                       previousMaterialRevision = 0;
        RestirSettings                                 previousRestir;
        uint32_t                                       previousMaxBounces = 0;
        bool                                           previousIbl = false;
        std::vector<LightData>                         previousLights;
        std::optional<EnvironmentMapAsset::ID>         previousEnvironment;

        void updateBufferDescriptors(RenderGraph& graph, const PassFrame& execution);
        void prepareRestir(RenderGraph& graph, const PassFrame& execution);
    };

    RealtimeScene scene;
    TraceStage    trace{parameters};
};
