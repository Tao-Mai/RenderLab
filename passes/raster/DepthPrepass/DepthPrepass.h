#pragma once

#include "render/raster/MeshDraw.h"
#include "render/raster/RasterAttachments.h"

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
    RasterAttachments<0> attachments;
    MeshCullPipelines    pipelines;
};
