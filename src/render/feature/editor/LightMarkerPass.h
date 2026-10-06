#pragma once

#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"

class LightMarkerPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
        RenderGraph::ImageHandle depth;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle colorHandle, RenderGraph::ImageHandle depthHandle);
    void   execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle depth;
    ShaderHandle             shader;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
};
