#pragma once

#include "render/feature/RenderGraph.h"
#include "render/feature/shadow/ShadowPass.h"

class TransparentPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&             graph,
        RenderGraph::ImageHandle colorHandle,
        RenderGraph::ImageHandle depthHandle,
        ShadowPass::Output       shadow
    );
    void execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle depth;
};
