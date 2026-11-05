#pragma once

#include <cstdint>
#include <utility>

#include <glm/vec2.hpp>

struct RgbChromaticities
{
    glm::dvec2 red{0.64, 0.33};
    glm::dvec2 green{0.30, 0.60};
    glm::dvec2 blue{0.15, 0.06};
    glm::dvec2 white{0.3127, 0.3290};
    bool operator==(const RgbChromaticities&) const = default;
};

enum class ImageFormat
{
    R8,
    RG8,
    RGB8,
    RGBA8,
    RGBA16F,
    RGBA32F,
    BC1,
    BC2,
    BC3,
    BC4Unorm,
    BC4Snorm,
    BC5Unorm,
    BC5Snorm,
    BC6HUfloat,
    BC6HSfloat,
    BC7,
};

struct ImageBlock
{
    uint32_t extent;
    uint32_t bytes;
};

[[nodiscard]] constexpr ImageBlock imageBlock(ImageFormat format)
{
    switch (format)
    {
        case ImageFormat::R8:
            return {1, 1};
        case ImageFormat::RG8:
            return {1, 2};
        case ImageFormat::RGB8:
            return {1, 3};
        case ImageFormat::RGBA8:
            return {1, 4};
        case ImageFormat::RGBA16F:
            return {1, 8};
        case ImageFormat::RGBA32F:
            return {1, 16};
        case ImageFormat::BC1:
        case ImageFormat::BC4Unorm:
        case ImageFormat::BC4Snorm:
            return {4, 8};
        case ImageFormat::BC2:
        case ImageFormat::BC3:
        case ImageFormat::BC5Unorm:
        case ImageFormat::BC5Snorm:
        case ImageFormat::BC6HUfloat:
        case ImageFormat::BC6HSfloat:
        case ImageFormat::BC7:
            return {4, 16};
    }
    std::unreachable();
}

[[nodiscard]] constexpr bool supportsSrgb(ImageFormat format)
{
    switch (format)
    {
        case ImageFormat::R8:
        case ImageFormat::RG8:
        case ImageFormat::RGB8:
        case ImageFormat::RGBA8:
        case ImageFormat::BC1:
        case ImageFormat::BC2:
        case ImageFormat::BC3:
        case ImageFormat::BC7:
            return true;
        default:
            return false;
    }
}

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
