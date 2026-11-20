#pragma once

#include "render/device/Device.h"

#include "render/raster/RasterAttachments.h"

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
    RasterAttachments<1> attachments;
    vk::Pipeline             pipeline;
    vk::PipelineLayout           pipelineLayout;
};
