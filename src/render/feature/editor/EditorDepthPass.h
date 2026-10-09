#pragma once

#include "render/feature/RenderGraph.h"

class EditorDepthPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle depth;
    };

    Output setup(RenderGraph& graph);
    void   execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle depth;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
};
