#pragma once

#include "render/feature/RenderGraph.h"

class EndPass
{
public:
    struct Output
    {
    };

    Output setup(
        RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle color, RenderGraph::BufferHandle readback
    );
    void execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const {}
};
