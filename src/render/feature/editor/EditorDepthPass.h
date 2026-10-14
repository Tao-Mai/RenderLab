#pragma once

#include "render/feature/RenderGraph.h"

class EditorDepthPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle depth;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup);
    void   execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle depth;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
};
