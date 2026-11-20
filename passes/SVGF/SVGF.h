#pragma once

#include "passes/SVGF/SVGFFilter.h"

class SVGF : public ConfigurablePass<SvgfSettings>
{
public:
    SVGFFilter::Output setup(RenderGraph& graph, const PassSetup& setup, RayTracingPass::Output traced)
    {
        const auto lighting = graph.addPass(reprojection, setup, traced);
        return filter.setup(graph, setup, traced, lighting, reprojection);
    }

private:
    SVGFReprojection reprojection{parameters};
    SVGFFilter       filter;
};
