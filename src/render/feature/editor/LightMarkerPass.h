#pragma once

#include "render/device/Device.h"

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
    ShaderHandle             shader;
    vk::Pipeline             pipeline;
    const PipelineLayout*    pipelineLayout = nullptr;
};
