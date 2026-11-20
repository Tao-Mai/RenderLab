#pragma once

#include "passes/raster/ForwardPbr.h"
#include "render/raster/RasterAttachments.h"
#include "render/raster/ShadowResources.h"

class ScenePass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&              graph,
        const PassSetup&          setup,
        RenderGraph::ImageHandle  target,
        RenderGraph::ImageHandle  depthHandle,
        ShadowResources           shadow,
        RenderGraph::BufferHandle lights
    );
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution);

private:
    RasterAttachments<1> attachments;
    ForwardPbrPipelines  pipelines;
};
