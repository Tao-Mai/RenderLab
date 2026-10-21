#include "render/ViewportRect.h"

#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <vulkan/vulkan.hpp>

ViewportRect ViewportRect::clamped(vk::Extent2D extent) const
{
    DCHECK(extent.width > 0 && extent.height > 0);

    const float targetWidth  = static_cast<float>(extent.width);
    const float targetHeight = static_cast<float>(extent.height);
    const float offsetX      = std::floor(std::clamp(x, 0.0f, targetWidth - 1.0f));
    const float offsetY      = std::floor(std::clamp(y, 0.0f, targetHeight - 1.0f));
    return {
        .x      = offsetX,
        .y      = offsetY,
        .width  = std::floor(std::clamp(width, 1.0f, targetWidth - offsetX)),
        .height = std::floor(std::clamp(height, 1.0f, targetHeight - offsetY)),
    };
}

vk::Viewport ViewportRect::toViewport() const
{
    return {.x = x, .y = y, .width = width, .height = height, .minDepth = 0.0f, .maxDepth = 1.0f};
}

vk::Rect2D ViewportRect::toScissor() const
{
    return {
        .offset = {static_cast<int32_t>(x), static_cast<int32_t>(y)},
        .extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
    };
}
