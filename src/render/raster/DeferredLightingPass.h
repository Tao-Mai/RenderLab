#pragma once

#include "render/device/Device.h"

#include "render/raster/GBufferPass.h"
#include "render/raster/ShadowPass.h"

class DeferredLightingPass
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
        GBufferPass::Output       gBuffer,
        ShadowPass::Output        shadow,
        RenderGraph::BufferHandle lights
    );
    void writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    GBufferPass::Output      gBuffer;
    RenderGraph::ImageHandle color;
    DescriptorSetLayout      descriptorSetLayout;
    DescriptorSetLayout      emptyDescriptorSetLayout;
    vk::raii::DescriptorSet  descriptorSet = nullptr;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
};
