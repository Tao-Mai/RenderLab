#pragma once

#include "passes/SVGF/SVGFFilter.h"

class SVGF : public ConfigurablePass<SvgfSettings>
{
public:
    SVGF() : ConfigurablePass(PassExecution::Composite) {}

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;

private:
    SVGFReprojection reprojection{parameters};
    SVGFFilter       filter;
};
