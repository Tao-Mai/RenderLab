#pragma once
#include <slang-rhi.h>

#include "passes/raytracing/realtime/RayTracingSettings.h"
#include "passes/raytracing/realtime/RestirResources.h"

#include <slang-rhi/shader-cursor.h>
#include "render/graph/RenderPass.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/raytracing/RealtimeScene.h"
#include "render/resources/ShaderLayout.h"

#include <array>
#include <optional>
#include <vector>

class RayTracingPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(RayTracingPass, "RayTracingPass", "Realtime path tracer with ReSTIR.");

    RayTracingPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<RayTracingPass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<RayTracingPass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    RayTracingSettings parameters;

    void prepareTargets(Falcor::RenderContext& context, const Falcor::RenderData& renderData);

    bool targetsReady = false;
    struct Program
    {
        rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
        rhi::ComPtr<rhi::IShaderTable>        shaderTable;
    };

    RealtimeScene scene;

    RestirResources                              reservoirs;
    std::array<Program, 2>                       programs;
    std::vector<rhi::ComPtr<rhi::IShaderObject>> shaderObjectsByFrame;
    GpuResources::ImageHandle                    historySource;
    RestirSettings                               previousRestir;
    uint32_t                                     sampleIndex        = 0;
    uint32_t                                     previousMaxBounces = 0;
};
