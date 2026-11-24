#pragma once

#include "core/MetadataAnnotations.h"

#include <cstdint>
#include <utility>

#include <glm/vec2.hpp>

struct RL_RECORD RgbChromaticities
{
    RL_FIELD("red") glm::dvec2 red{0.64, 0.33};
    RL_FIELD("green") glm::dvec2 green{0.30, 0.60};
    RL_FIELD("blue") glm::dvec2 blue{0.15, 0.06};
    RL_FIELD("white") glm::dvec2 white{0.3127, 0.3290};
    bool operator==(const RgbChromaticities&) const = default;
};

enum class RL_ENUM ImageFormat
{
    R8 RL_ENUM_VALUE("R8"),
    RG8 RL_ENUM_VALUE("RG8"),
    RGB8 RL_ENUM_VALUE("RGB8"),
    RGBA8 RL_ENUM_VALUE("RGBA8"),
    RGBA16F RL_ENUM_VALUE("RGBA16F"),
    RGBA32F RL_ENUM_VALUE("RGBA32F"),
    BC1 RL_ENUM_VALUE("BC1"),
    BC2 RL_ENUM_VALUE("BC2"),
    BC3 RL_ENUM_VALUE("BC3"),
    BC4Unorm RL_ENUM_VALUE("BC4Unorm"),
    BC4Snorm RL_ENUM_VALUE("BC4Snorm"),
    BC5Unorm RL_ENUM_VALUE("BC5Unorm"),
    BC5Snorm RL_ENUM_VALUE("BC5Snorm"),
    BC6HUfloat RL_ENUM_VALUE("BC6HUfloat"),
    BC6HSfloat RL_ENUM_VALUE("BC6HSfloat"),
    BC7 RL_ENUM_VALUE("BC7"),
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

enum class RL_ENUM ColorSpace
{
    Linear RL_ENUM_VALUE("Linear"),
    Srgb RL_ENUM_VALUE("Srgb")
};

enum class RL_ENUM ImageLayout
{
    Image2D RL_ENUM_VALUE("Image2D"),
    Cubemap RL_ENUM_VALUE("Cubemap"),
};
