#pragma once

#include "render/feature/RenderGraph.h"

class EditorPickingCopyPass
{
public:
    struct Output
    {
        RenderGraph::BufferHandle readback;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle pickingImageHandle);
    void   execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle  pickingImage;
    RenderGraph::BufferHandle readback;
};
