#pragma once

#include "render/ViewportRect.h"
#include "render/graph/RenderGraph.h"

#include <array>
#include <cstddef>
#include <optional>

struct RasterColorAttachment
{
    RenderGraph::ImageHandle image;
    vk::AttachmentLoadOp     loadOp = vk::AttachmentLoadOp::eClear;

    void                                      declare(RenderGraph& graph) const;
    [[nodiscard]] vk::RenderingAttachmentInfo renderingInfo(RenderGraph& graph) const;
};

struct RasterDepthAttachment
{
    RenderGraph::ImageHandle image;
    bool                     readOnly = true;

    void                                      declare(RenderGraph& graph) const;
    [[nodiscard]] vk::RenderingAttachmentInfo renderingInfo(RenderGraph& graph) const;

private:
    [[nodiscard]] vk::ImageLayout layout() const
    {
        return readOnly ? vk::ImageLayout::eDepthReadOnlyOptimal : vk::ImageLayout::eDepthAttachmentOptimal;
    }
};

template <size_t ColorCount>
struct RasterAttachments
{
    std::array<RasterColorAttachment, ColorCount> colors;
    std::optional<RasterDepthAttachment>          depth;

    void declare(RenderGraph& graph) const
    {
        for (const auto& color : colors)
            color.declare(graph);
        if (depth)
            depth->declare(graph);
    }

    ViewportRect
    begin(RenderGraph& graph, vk::raii::CommandBuffer& commands, std::optional<ViewportRect> view = std::nullopt) const
    {
        DCHECK(ColorCount != 0 || depth);
        const auto extent = graph.imageExtent(ColorCount != 0 ? colors.front().image : depth->image);
        std::array<vk::RenderingAttachmentInfo, ColorCount> colorInfos;
        for (size_t index = 0; index < colors.size(); ++index)
            colorInfos[index] = colors[index].renderingInfo(graph);
        const auto depthInfo = depth ? depth->renderingInfo(graph) : vk::RenderingAttachmentInfo{};
        commands.beginRendering({
            .renderArea           = {{0, 0}, extent},
            .layerCount           = 1,
            .colorAttachmentCount = static_cast<uint32_t>(ColorCount),
            .pColorAttachments    = colorInfos.data(),
            .pDepthAttachment     = depth ? &depthInfo : nullptr,
        });

        const auto viewport =
            view.value_or(ViewportRect{0, 0, float(extent.width), float(extent.height)}).clamped(extent);
        commands.setViewport(0, viewport.toViewport());
        commands.setScissor(0, viewport.toScissor());
        return viewport;
    }
};
