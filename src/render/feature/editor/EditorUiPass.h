#pragma once

#include "render/feature/RenderGraph.h"

class EditorUiPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle colorHandle);
    void   execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle color;
};
