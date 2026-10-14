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

    Output setup(RenderGraph& graph, const PassSetup& setup);
    void   execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle depth;
    ShaderHandle             shader;
    vk::PipelineLayout       pipelineLayout;
};
