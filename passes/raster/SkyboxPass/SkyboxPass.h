#pragma once

#include "render/graph/RenderPass.h"

#include "render/device/Device.h"

#include "render/raster/RasterAttachments.h"

class SkyboxPass : public RenderPass
{
public:

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    RasterAttachments<1> attachments;
    vk::Pipeline             pipeline;
    vk::PipelineLayout           pipelineLayout;
};
