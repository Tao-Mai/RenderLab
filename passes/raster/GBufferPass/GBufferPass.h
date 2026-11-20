#pragma once

#include "render/graph/RenderPass.h"

#include "render/raster/MeshDraw.h"
#include "render/raster/RasterAttachments.h"

#include <array>

class GBufferPass : public RenderPass
{
public:

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    static constexpr std::array kColorFormats{
        vk::Format::eR8G8B8A8Srgb,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Format::eR16G16B16A16Sfloat,
    };
    RasterAttachments<kColorFormats.size()> attachments;
    MeshCullPipelines                       pipelines;
};
