#pragma once
#include <slang-rhi.h>

#include "passes/raytracing/restir/RayTracingSettings.h"
#include "passes/raytracing/restir/RestirResources.h"

#include <slang-rhi/shader-cursor.h>
#include "render/GpuScene.h"
#include "render/graph/RenderPassSettings.h"
#include "render/raytracing/PathTracingShader.h"

#include <array>

class RayTracingPass : public RenderPass
{
public:
    RayTracingPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<RayTracingPass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<RayTracingPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const ResourceCache::DefaultProperties& defaults) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;
    void setScene(GpuScene* scene) override;
    void onSceneUpdates(RenderContext*, SceneUpdateFlags updates) override { sceneUpdates |= updates; }

private:
    RayTracingSettings parameters;

    void onSettingsUpdated(const RayTracingSettings& previous);

    void prepareTargets(const RenderData& renderData);

    bool targetsReady = false;
    struct Program
    {
        rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
        rhi::ComPtr<rhi::IShaderTable> shaderTable;
        std::array<rhi::ComPtr<rhi::IShaderObject>, kMaxFramesInFlight> shaderObjects;
    };
    void prepareProgram(
        Program& program, const RenderContext& context, const RenderData& renderData,
        bool hasEnvironment
    );

    RestirResources                              reservoirs;
    std::array<Program, kPathEnvironmentVariantCount> programs;
    SceneUpdateFlags                             sceneUpdates            = SceneUpdateFlags::None;
    RestirSettings                               previousRestir;
    uint32_t                                     sampleIndex             = 0;
    uint32_t                                     previousMaxBounces       = 0;

    static bool drawSettings(
        Gui::Widgets& widgets, RayTracingSettings& settings, const RayTracingPass& pass, const PassEditorContext& context
    );

    RL_PLUGIN_CLASS(
        RayTracingPass,
        "RayTracingPass",
        .desc = "Path tracer with ReSTIR.",
        .settings = pass_settings::bind<
            &RayTracingPass::parameters,
            &RayTracingPass::drawSettings,
            &RayTracingPass::onSettingsUpdated
        >()
    );
};
