#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPassSettings.h"

#include <slang-rhi/shader-cursor.h>
#include "shaders/passes/raster/RasterPbr.h"
#include "render/raster/RasterLightingSettings.h"

class ScenePass : public RenderPass
{
public:
    ScenePass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<ScenePass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<ScenePass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const ResourceCache::DefaultProperties& defaults) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    RasterLightingOutputSettings parameters;

    void onSettingsUpdated(const RasterLightingOutputSettings& previous);

    ForwardPbrPipelines pipelines;

    RL_PLUGIN_CLASS(
        ScenePass,
        "ScenePass",
        .desc = "Renders opaque geometry with forward shading.",
        .settings = pass_settings::bind<&ScenePass::parameters, nullptr, &ScenePass::onSettingsUpdated>()
    );
};
