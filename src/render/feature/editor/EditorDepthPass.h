#pragma once

#include "render/device/Device.h"

#include "render/feature/RenderGraph.h"

class EditorDepthPass
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
    vk::Pipeline             pipeline;
    const PipelineLayout*    pipelineLayout = nullptr;
};
