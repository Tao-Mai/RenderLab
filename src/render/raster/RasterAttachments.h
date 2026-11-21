#pragma once

#include "core/Logger.h"
#include "render/ViewportRect.h"
#include "render/device/VulkanResources.h"

#include <array>
#include <cstddef>
#include <optional>

struct RasterColorAttachment
{
    VulkanResources::ImageHandle image;
    vk::AttachmentLoadOp     loadOp = vk::AttachmentLoadOp::eClear;

    [[nodiscard]] vk::RenderingAttachmentInfo renderingInfo(VulkanResources& cache) const;
};

struct RasterDepthAttachment
{
    VulkanResources::ImageHandle image;
    bool                     readOnly = true;

    [[nodiscard]] vk::RenderingAttachmentInfo renderingInfo(VulkanResources& cache) const;

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

    ViewportRect begin(
        VulkanResources& cache, vk::raii::CommandBuffer& commands, std::optional<ViewportRect> view = std::nullopt
    ) const
    {
        DCHECK(ColorCount != 0 || depth);
        const auto extent = cache.imageExtent(ColorCount != 0 ? colors.front().image : depth->image);
        std::array<vk::RenderingAttachmentInfo, ColorCount> colorInfos;
        for (size_t index = 0; index < colors.size(); ++index)
            colorInfos[index] = colors[index].renderingInfo(cache);
        const auto depthInfo = depth ? depth->renderingInfo(cache) : vk::RenderingAttachmentInfo{};
        commands.beginRendering({
            .renderArea           = {{0, 0}, extent},
            .layerCount           = 1,
            .colorAttachmentCount = static_cast<uint32_t>(ColorCount),
            .pColorAttachments    = colorInfos.data(),
            .pDepthAttachment     = depth ? &depthInfo : nullptr,
        });

        const auto viewport =
            view ? view->clamped(extent) : ViewportRect{0, 0, float(extent.width), float(extent.height)};
        commands.setViewport(0, viewport.toViewport());
        commands.setScissor(0, viewport.toScissor());
        return viewport;
    }
};
