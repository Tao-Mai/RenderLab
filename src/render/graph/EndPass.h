#pragma once

#include "render/graph/RenderGraph.h"

class EndPass
{
public:
    struct Output
    {
    };

    Output setup(
        RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle color, RenderGraph::BufferHandle readback
    );
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const {}
};
