#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPassSettings.h"

#include <slang-rhi/shader-cursor.h>
#include "shaders/passes/raster/RasterPbr.h"
#include "render/raster/RasterLightingSettings.h"

#include <vector>

class TransparentPass : public RenderPass
{
public:
    TransparentPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<TransparentPass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<TransparentPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const ResourceCache::DefaultProperties&) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    void sortDrawItems();

    TransparentLightingSettings parameters;
    std::vector<uint32_t>        sortedDrawItems;

    ForwardPbrPipelines pipelines;

    RL_PLUGIN_CLASS(
        TransparentPass,
        "TransparentPass",
        .desc = "Renders transparent geometry.",
        .settings = pass_settings::bind<&TransparentPass::parameters>()
    );
};
