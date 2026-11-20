#pragma once

#include "render/graph/RenderPass.h"

#include "render/raster/MeshDraw.h"
#include "render/raster/RasterAttachments.h"

class DepthPrepass : public RenderPass
{
public:

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    RasterAttachments<0> attachments;
    MeshCullPipelines    pipelines;
};
