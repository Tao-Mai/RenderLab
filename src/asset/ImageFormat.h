#pragma once

enum class ImageFormat
{
    R8,
    RG8,
    RGB8,
    RGBA8,
    RGBA16F,
    RGBA32F,
};

enum class ColorSpace
{
    Linear,
    Srgb
};

enum class ImageLayout
{
    Image2D,
    Cubemap,
};
