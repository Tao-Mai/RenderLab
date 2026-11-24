#pragma once
#include <slang-rhi.h>

#include "passes/denoise/SVGF/SVGFFilter.h"
#include "render/graph/RenderPass.h"

#include <array>

class SVGFPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(SVGFPass, "SVGFPass", "SVGF denoising pass.");

    SVGFPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<SVGFPass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<SVGFPass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    SvgfSettings parameters;

    SVGFReprojection                                            reprojection{parameters};
    SVGFFilter                                                  filter;
    std::array<rhi::ComPtr<rhi::ITexture>, kLightingLayerCount> reprojected;
    std::array<rhi::ComPtr<rhi::ITexture>, kLightingLayerCount> scratch;
    rhi::ComPtr<rhi::ITexture>                                  historyLength;
    bool                                                        resourcesBound = false;
};
