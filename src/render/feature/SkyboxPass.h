#pragma once

#include "render/feature/RenderGraph.h"

#include <memory>

class GpuTexture;

class SkyboxPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle colorHandle, RenderGraph::ImageHandle depthHandle);
    void   refreshBindings(RenderGraph& graph);
    void   execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle    color;
    RenderGraph::ImageHandle    depth;
    std::shared_ptr<GpuTexture> radianceTexture;
    vk::Pipeline                pipeline;
    vk::PipelineLayout          pipelineLayout;
};
