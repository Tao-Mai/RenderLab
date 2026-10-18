#pragma once

#include "render/feature/RenderGraph.h"

class EditorUiPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle colorHandle);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle color;
};
