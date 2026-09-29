#pragma once

#include "asset/Asset.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

namespace asset_import::environment_map
{
using Rgba = std::array<float, 4>;

// Cubemap faces use +X, -X, +Y, -Y, +Z, -Z order.
[[nodiscard]] glm::vec3 faceDirection(uint32_t face, float u, float v);

// The input is row-major, interleaved linear RGBA32F.
[[nodiscard]] Rgba sampleEquirectangular(
    const float* image, uint32_t width, uint32_t height, const glm::vec3& direction);
[[nodiscard]] Rgba sampleOpenExrCube(
    const float* image, uint32_t side, uint32_t face, uint32_t x, uint32_t y);

struct LinearCubemapView
{
    uint32_t                     side;
    uint32_t                     mipLevels;
    std::array<const float*, 32> levels{};
};

// The input mip chain is mip-major, then face-major, then row-major RGBA32F.
[[nodiscard]] LinearCubemapView linearCubemapView(
    const Texture::Desc& radiance, std::span<const float> radiancePixels);
[[nodiscard]] float mipLevelForSolidAngle(
    float sampleSolidAngle, uint32_t side, uint32_t mipLevels);
[[nodiscard]] Rgba sampleRadiance(
    const LinearCubemapView& cubemap, const glm::vec3& direction, float lod);

[[nodiscard]] ImageFormat environmentFormat(ImageFormat format);
void writePixel(uint8_t* output, const Rgba& rgba,
                ImageFormat format, ColorSpace colorSpace);
[[nodiscard]] std::vector<float> decode8BitPixels(
    std::span<const uint8_t> pixels, uint32_t width, uint32_t height,
    ImageFormat format, ColorSpace colorSpace);
}
