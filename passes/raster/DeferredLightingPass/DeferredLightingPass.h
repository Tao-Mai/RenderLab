#pragma once

#include "render/graph/RenderPass.h"

#include "render/device/Device.h"

#include "render/raster/GBufferResources.h"
#include "render/raster/RasterAttachments.h"
#include "render/raster/ShadowResources.h"

class DeferredLightingPass : public RenderPass
{
public:

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void bindResources(RenderGraph& graph, const PassSetup& setup) override;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    GBufferResources      gBuffer;
    RasterAttachments<1>     attachments;
    DescriptorSetLayout      descriptorSetLayout;
    vk::raii::DescriptorSet  descriptorSet = nullptr;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
};
