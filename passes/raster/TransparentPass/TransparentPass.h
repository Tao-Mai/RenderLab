#pragma once

#include "passes/raster/ForwardPbr.h"
#include "render/raster/RasterAttachments.h"
#include "render/raster/ShadowResources.h"

#include <vector>

class TransparentPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&              graph,
        const PassSetup&          setup,
        RenderGraph::ImageHandle  colorHandle,
        RenderGraph::ImageHandle  depthHandle,
        ShadowResources           shadow,
        RenderGraph::BufferHandle lights
    );
    void prepare(RenderGraph& graph, const PassFrame& frame);
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution);

private:
    std::vector<uint32_t> sortedDrawItems;
    RasterAttachments<1>  attachments;
    ForwardPbrPipelines   pipelines;
};
