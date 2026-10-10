#pragma once

#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"

#include <array>

class GBufferPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle albedoOcclusion;
        RenderGraph::ImageHandle normalRoughness;
        RenderGraph::ImageHandle emissiveMetallic;
        RenderGraph::ImageHandle depth;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle depthHandle);
    void   execute(RenderGraph& graph) const;

private:
    static constexpr std::array kColorFormats{
        vk::Format::eR8G8B8A8Srgb,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Format::eR16G16B16A16Sfloat,
    };
    std::array<RenderGraph::ImageHandle, kColorFormats.size()> colors;
    RenderGraph::ImageHandle                                   depth;
    ShaderHandle                                               shader;
};
