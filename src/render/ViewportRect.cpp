#include "render/ViewportRect.h"

#include "core/Logger.h"

#include <algorithm>
#include <cmath>

ViewportRect ViewportRect::clamped(Extent2D extent) const
{

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

PixelBounds ViewportRect::pixelBounds(Extent2D extent) const
{
    const auto firstX = std::clamp(int32_t(std::ceil(x - 0.5f)), 0, int32_t(extent.width));
    const auto firstY = std::clamp(int32_t(std::ceil(y - 0.5f)), 0, int32_t(extent.height));
    const auto endX   = std::clamp(int32_t(std::ceil(x + width - 0.5f)), firstX, int32_t(extent.width));
    const auto endY   = std::clamp(int32_t(std::ceil(y + height - 0.5f)), firstY, int32_t(extent.height));
    return {{uint32_t(firstX), uint32_t(firstY)}, {uint32_t(endX - firstX), uint32_t(endY - firstY)}};
}
