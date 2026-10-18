#pragma once
#include "render/feature/SceneDraw.h"

#include "render/feature/RenderGraph.h"
#include "render/feature/shadow/ShadowPass.h"

class TransparentPass
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
        RenderGraph::ImageHandle depthHandle,
        ShadowPass::Output       shadow
    );
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    SceneShaderOptions       shaderOptions;
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle depth;
};
