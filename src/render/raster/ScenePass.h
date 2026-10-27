#pragma once
#include "render/raster/SceneDraw.h"

class ScenePass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
        RenderGraph::ImageHandle depth;
        ShadowPass::Output       shadow;
    };

    Output setup(
        RenderGraph&              graph,
        const PassSetup&          setup,
        RenderGraph::ImageHandle  target,
        RenderGraph::ImageHandle  depthHandle,
        ShadowPass::Output        shadow,
        RenderGraph::BufferHandle lights
    );
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    SceneDrawOptions         shaderOptions;
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle depth;
};
