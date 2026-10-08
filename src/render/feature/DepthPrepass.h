#pragma once

#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"

class DepthPrepass
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
    ShaderHandle             shader;
    vk::PipelineLayout       pipelineLayout;
};
