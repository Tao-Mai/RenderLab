#pragma once

#include "render/graph/RenderPass.h"

#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>
#include "render/raster/RasterLightingSettings.h"

#include <array>

class DeferredLightingPass : public RenderPass
{
    RL_PLUGIN_CLASS(DeferredLightingPass, "DeferredLightingPass", "Shades the G-buffer.");

    DeferredLightingPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<DeferredLightingPass>
    create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<DeferredLightingPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    RasterLightingOutputSettings parameters;

    rhi::IRenderPipeline* getPipeline(const RenderContext& context, rhi::Format format);

    std::array<rhi::ComPtr<rhi::IRenderPipeline>, 2> pipelines;
};
