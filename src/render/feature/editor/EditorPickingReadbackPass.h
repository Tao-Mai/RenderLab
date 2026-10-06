#pragma once

#include "render/feature/RenderGraph.h"

class EditorPickingReadbackPass
{
public:
    struct Output
    {
    };

    Output                 setup(RenderGraph& graph, RenderGraph::BufferHandle readbackHandle);
    void                   execute(RenderGraph& graph) const;
    [[nodiscard]] uint32_t readSelectionId(RenderGraph& graph, uint32_t frameIndex) const;

private:
    RenderGraph::BufferHandle readback;
};
