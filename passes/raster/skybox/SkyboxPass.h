#pragma once

#include "render/device/Device.h"

#include "render/graph/RenderGraph.h"

class SkyboxPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&             graph,
        const PassSetup&         setup,
        RenderGraph::ImageHandle colorHandle,
        RenderGraph::ImageHandle depthHandle
    );
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle depth;
    vk::Pipeline             pipeline;
    vk::PipelineLayout           pipelineLayout;
};
