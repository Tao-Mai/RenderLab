#pragma once

#include "render/feature/SceneBindings.h"
#include "render/feature/deferred/GBufferPass.h"

#include <vector>

class DeferredLightingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
        RenderGraph::ImageHandle depth;
        ShadowPass::Output       shadow;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, GBufferPass::Output gBuffer, ShadowPass::Output shadow);
    void   prepare(RenderGraph& graph, const PassSetup& setup) const;
    void   execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const;

    SceneBindings& sceneBindings() { return bindings; }

private:
    SceneBindings                        bindings;
    GBufferPass::Output                  gBuffer;
    RenderGraph::ImageHandle             color;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    vk::Pipeline                         pipeline;
    vk::PipelineLayout                   pipelineLayout;
};
