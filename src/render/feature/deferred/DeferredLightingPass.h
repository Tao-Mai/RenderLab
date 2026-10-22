#pragma once

#include "render/device/Device.h"

#include "render/feature/SceneBindings.h"
#include "render/feature/deferred/GBufferPass.h"

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
    void   writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

    SceneBindings& sceneBindings() { return bindings; }

private:
    SceneBindings            bindings;
    GBufferPass::Output      gBuffer;
    RenderGraph::ImageHandle color;
    DescriptorSetLayout      descriptorSetLayout;
    DescriptorSetLayout      emptyDescriptorSetLayout;
    vk::raii::DescriptorSet  descriptorSet = nullptr;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
};
