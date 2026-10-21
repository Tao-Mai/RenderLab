#pragma once

namespace vk
{
struct Extent2D;
struct Rect2D;
struct Viewport;
}  // namespace vk

struct ViewportRect
{
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 1.0f;
    float height = 1.0f;

    [[nodiscard]] ViewportRect clamped(vk::Extent2D extent) const;
    [[nodiscard]] vk::Viewport toViewport() const;
    [[nodiscard]] vk::Rect2D   toScissor() const;
};
