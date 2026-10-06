#pragma once

#include "render/feature/RenderGraph.h"

class RayTracingBlitPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle rayImageHandle, RenderGraph::ImageHandle colorHandle);
    void   execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle rayImage;
    RenderGraph::ImageHandle color;
};
