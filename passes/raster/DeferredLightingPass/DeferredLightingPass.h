#pragma once

#include "render/device/Device.h"

#include "passes/raster/GBufferPass/GBufferPass.h"
#include "render/raster/RasterAttachments.h"
#include "render/raster/ShadowResources.h"

class DeferredLightingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&              graph,
        const PassSetup&          setup,
        RenderGraph::ImageHandle  target,
        GBufferPass::Output       gBuffer,
        ShadowResources           shadow,
        RenderGraph::BufferHandle lights
    );
    void writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    GBufferPass::Output      gBuffer;
    RasterAttachments<1>     attachments;
    DescriptorSetLayout      descriptorSetLayout;
    vk::raii::DescriptorSet  descriptorSet = nullptr;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
};
