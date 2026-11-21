#pragma once

#include "render/graph/RenderPass.h"

#include "passes/postprocess/ToneMappingPass/ToneMappingSettings.h"
#include "render/graph/PassSettings.h"

#include "render/device/Device.h"
#include "render/raster/RasterAttachments.h"

class ToneMappingPass : public ConfigurablePass<ToneMappingSettings>
{
public:

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void bindResources(RenderGraph& graph, const PassSetup& setup) override;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    RenderGraph::ImageHandle  hdrColor;
    RasterAttachments<1>      attachments;
    RenderGraph::BufferHandle exposure;
    DescriptorSetLayout       descriptorSetLayout;
    vk::raii::DescriptorSet   descriptorSet = nullptr;
    vk::Sampler               sampler;
    vk::Pipeline              pipeline;
    vk::PipelineLayout        pipelineLayout;
    bool                      encodeSrgb = false;
};
