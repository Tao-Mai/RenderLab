#pragma once
#include <slang-rhi.h>

#include "passes/denoise/SVGF/SVGFFilter.h"
#include "render/graph/RenderPass.h"

#include <array>

class SVGFPass : public RenderPass
{
    RL_PLUGIN_CLASS(SVGFPass, "SVGFPass", "SVGF denoising pass.");

    SVGFPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<SVGFPass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<SVGFPass>(std::move(device), properties);
    }

    void renderUI(RenderContext* context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    SvgfSettings parameters;

    SVGFReprojection                                            reprojection{parameters};
    SVGFFilter                                                  filter;
    std::array<rhi::ComPtr<rhi::ITexture>, kLightingLayerCount> reprojected;
    std::array<rhi::ComPtr<rhi::ITexture>, kLightingLayerCount> scratch;
    rhi::ComPtr<rhi::ITexture>                                  historyLength;
    bool                                                        resourcesBound = false;
};
