#pragma once

#include "render/device/Device.h"

#include "render/feature/RenderGraph.h"

class DepthPrepass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle depth;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle depth;
    vk::Pipeline             singleSidedPipeline;
    vk::Pipeline             doubleSidedPipeline;
    const PipelineLayout*    pipelineLayout = nullptr;
};
