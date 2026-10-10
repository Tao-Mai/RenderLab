#pragma once

#include "render/feature/SceneBindings.h"

class ScenePass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
        RenderGraph::ImageHandle depth;
        ShadowPass::Output       shadow;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle depthHandle, ShadowPass::Output shadow);
    void   prepare(RenderGraph& graph) const;
    void   updateFrame(FrameData& frame, const ViewUniforms& view, const RenderData& scene) const;
    void   execute(RenderGraph& graph) const;

private:
    SceneBindings            bindings;
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle depth;
};
