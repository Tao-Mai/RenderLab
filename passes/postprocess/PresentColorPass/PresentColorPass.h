#pragma once

#include "render/graph/RenderGraph.h"

class PresentColorPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&             graph,
        const PassSetup&         setup,
        RenderGraph::ImageHandle source,
        RenderGraph::ImageHandle target
    );

private:
    class BlitPass
    {
    public:
        Output setup(
            RenderGraph& graph, const PassSetup& setup,
            RenderGraph::ImageHandle sourceHandle, RenderGraph::ImageHandle targetHandle
        );
        void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame&) const;

    private:
        RenderGraph::ImageHandle source;
        RenderGraph::ImageHandle target;
    };

    BlitPass blit;
};
