#pragma once

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
