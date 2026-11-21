#pragma once

#include "passes/denoise/SVGF/SVGFFilter.h"
#include "render/graph/PassSettings.h"

class SVGF : public ConfigurablePass<SvgfSettings, CompositeRenderPass>
{
public:
    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;

private:
    SVGFReprojection reprojection{parameters};
    SVGFFilter       filter;
};
