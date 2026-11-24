#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "passes/raster/RasterPbr.h"
#include "render/raster/RasterLightingSettings.h"

#include <vector>

class TransparentPass : public RenderPass
{
    RL_PLUGIN_CLASS(TransparentPass, "TransparentPass", "Renders transparent geometry.");

    TransparentPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<TransparentPass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<TransparentPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    void sortDrawItems(const RenderContext& frame);

    TransparentLightingSettings parameters;
    std::vector<uint32_t>        sortedDrawItems;

    ForwardPbrPipelines pipelines;
};
