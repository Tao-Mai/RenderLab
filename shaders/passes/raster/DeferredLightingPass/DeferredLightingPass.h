#pragma once

#include "render/graph/RenderPassSettings.h"

#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>
#include "render/raster/RasterLightingSettings.h"

#include <array>

class DeferredLightingPass : public RenderPass
{
public:
    DeferredLightingPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<DeferredLightingPass>
    create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<DeferredLightingPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const ResourceCache::DefaultProperties& defaults) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    RasterLightingOutputSettings parameters;

    void onSettingsUpdated(const RasterLightingOutputSettings& previous);

    rhi::IRenderPipeline* getPipeline(const RenderContext& context, rhi::Format format, bool hasEnvironment);

    std::array<rhi::ComPtr<rhi::IRenderPipeline>, 2> pipelines;

    RL_PLUGIN_CLASS(
        DeferredLightingPass,
        "DeferredLightingPass",
        .desc = "Shades the G-buffer.",
        .settings = pass_settings::bind<
            &DeferredLightingPass::parameters,
            nullptr,
            &DeferredLightingPass::onSettingsUpdated
        >()
    );
};
