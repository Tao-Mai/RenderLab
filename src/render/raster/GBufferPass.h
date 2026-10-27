#pragma once

#include "render/resources/ShaderManager.h"
#include "render/graph/RenderGraph.h"

#include <array>

class GBufferPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle albedoOcclusion;
        RenderGraph::ImageHandle normalRoughness;
        RenderGraph::ImageHandle emissiveMetallic;
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
    };
    std::array<RenderGraph::ImageHandle, kColorFormats.size()> colors;
    RenderGraph::ImageHandle                                   depth;
    ShaderHandle                                               shader;
};
