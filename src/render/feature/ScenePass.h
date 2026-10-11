#pragma once
#include "render/feature/SceneDraw.h"

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
    void   execute(RenderGraph& graph) const;

    SceneBindings& sceneBindings() { return bindings; }

private:
    SceneShaderOptions       shaderOptions;
    SceneBindings            bindings;
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle depth;
};
