#pragma once

#include "render/graph/RenderGraph.h"

class EndPass
{
public:
    void compile(
        RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle color, RenderGraph::BufferHandle readback
    );
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const {}
};
