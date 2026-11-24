#pragma once
#include <cstdint>
#include <slang-rhi.h>
struct Extent2D
{
    uint32_t width                             = 0;
    uint32_t height                            = 0;
    bool     operator==(const Extent2D&) const = default;
};
struct PixelBounds
{
    struct Offset
    {
        uint32_t x;
        uint32_t y;
    } offset;
    Extent2D extent;
};
struct ViewportRect
{
    float                          x      = 0.0f;
    float                          y      = 0.0f;
    float                          width  = 1.0f;
    float                          height = 1.0f;

    bool operator==(const ViewportRect&) const = default;

    [[nodiscard]] ViewportRect     clamped(Extent2D extent) const;
    [[nodiscard]] PixelBounds      pixelBounds(Extent2D extent) const;
    [[nodiscard]] rhi::Viewport    toViewport() const { return {x, y, width, height, 0.0f, 1.0f}; }
    [[nodiscard]] rhi::RenderState renderState() const
    {
        rhi::RenderState state;
        state.viewportCount    = 1;
        state.viewports[0]     = toViewport();
        state.scissorRectCount = 1;
        state.scissorRects[0]  = toScissor();
        return state;
    }
    [[nodiscard]] rhi::ScissorRect toScissor() const
    {
        return {uint32_t(x), uint32_t(y), uint32_t(x + width), uint32_t(y + height)};
    }
};
