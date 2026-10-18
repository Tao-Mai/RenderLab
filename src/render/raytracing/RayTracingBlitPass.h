#pragma once

#include "render/feature/RenderGraph.h"

class RayTracingBlitPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&             graph,
        const PassSetup&         setup,
        RenderGraph::ImageHandle rayImageHandle,
        RenderGraph::ImageHandle colorHandle
    );
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle rayImage;
    RenderGraph::ImageHandle color;
};
