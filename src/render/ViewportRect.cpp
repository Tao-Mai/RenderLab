#include "render/ViewportRect.h"

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
