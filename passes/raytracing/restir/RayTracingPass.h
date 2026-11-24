#pragma once
#include <slang-rhi.h>

#include "passes/raytracing/restir/RayTracingSettings.h"
#include "passes/raytracing/restir/RestirResources.h"

#include <slang-rhi/shader-cursor.h>
#include "render/graph/RenderPass.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/ShaderLayout.h"

#include <array>

class RayTracingPass : public RenderPass
{
    RL_PLUGIN_CLASS(RayTracingPass, "RayTracingPass", "Path tracer with ReSTIR.");

    RayTracingPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<RayTracingPass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<RayTracingPass>(std::move(device), properties);
    }

    void renderUI(RenderContext* context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    RayTracingSettings parameters;

    void prepareTargets(RenderContext& context, const RenderData& renderData);

    bool targetsReady = false;
    struct Program
    {
        PathTracingProgram rayTracing;
        std::array<rhi::ComPtr<rhi::IShaderObject>, kMaxFramesInFlight> shaderObjects;
    };
    void prepareProgram(
        Program& program, const RenderContext& context, const RenderData& renderData,
        PathEnvironment environment
    );

    RestirResources                              reservoirs;
    std::array<Program, static_cast<size_t>(PathEnvironment::Count)> programs;
    GpuResources::ImageHandle                    historySource;
    RestirSettings                               previousRestir;
    uint32_t                                     sampleIndex             = 0;
    uint32_t                                     previousMaxBounces       = 0;
    uint64_t                                     previousSceneGeneration = 0;
};
