#pragma once

#include "render/raster/MeshDraw.h"
#include "render/raster/RasterAttachments.h"

#include <array>

class GBufferPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle diffuseOcclusion;
        RenderGraph::ImageHandle normalRoughness;
        RenderGraph::ImageHandle emissiveSpecularF90;
        RenderGraph::ImageHandle specularF0DiffuseFresnel;
        RenderGraph::ImageHandle sheenColorRoughness;
        RenderGraph::ImageHandle depth;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle depthHandle);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

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
