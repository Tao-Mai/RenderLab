#pragma once

#include "render/graph/RenderPass.h"

#include "passes/raster/ForwardPbr.h"
#include "render/raster/RasterAttachments.h"
#include "render/raster/ShadowResources.h"

class ScenePass : public RenderPass
{
public:

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    RasterAttachments<1> attachments;
    ForwardPbrPipelines  pipelines;
};
