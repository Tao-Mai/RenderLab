#pragma once

#include "render/feature/RenderGraph.h"

class EndPass
{
public:
    struct Output
    {
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle color, RenderGraph::BufferHandle readback);
    void   execute(RenderGraph&) const {}
};
