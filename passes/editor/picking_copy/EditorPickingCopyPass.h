#pragma once

#include "render/graph/RenderGraph.h"

class EditorPickingCopyPass
{
public:
    struct Output
    {
        RenderGraph::BufferHandle readback;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle pickingImageHandle);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle  pickingImage;
    RenderGraph::BufferHandle readback;
};
