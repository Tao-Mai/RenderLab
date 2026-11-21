#pragma once

#include "render/device/ResourceBarriers.h"
#include "passes/raytracing/realtime/RayTracingSettings.h"
#include "passes/raytracing/realtime/RestirResources.h"

#include "render/graph/RenderPass.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/raytracing/RayTracingPipeline.h"
#include "render/raytracing/RealtimeSceneBindings.h"

#include <array>
#include <optional>
#include <vector>

class RayTracingPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(RayTracingPass, "RayTracingPass", "Realtime path tracer with ReSTIR.");

    RayTracingPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<RayTracingPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<RayTracingPass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    RayTracingSettings parameters;

    void prepareTargets(Falcor::RenderContext& context, const Falcor::RenderData& renderData);

    ResourceBarriers barriers;
    struct Program
    {
        DescriptorSetLayout               descriptorLayout;
        std::optional<RayTracingPipeline> pipeline;
    };

    RealtimeScene scene;
    RealtimeSceneBindings                      sceneBindings;
    ResourceBarriers                       traceAccesses;
    RestirResources                            reservoirs;
    std::array<Program, 2>                     programs;
    ShaderPushConstants<PathTracingParameters> frameConstants;
    std::vector<vk::raii::DescriptorSet>       descriptorSets;
    VulkanResources::ImageHandle                 historySource;
    RestirSettings                             previousRestir;
    uint32_t                                   sampleIndex        = 0;
    uint32_t                                   previousMaxBounces = 0;
};
