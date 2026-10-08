#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <vector>

struct TextureAsset;

namespace texture_mip
{
// Keeps mip 0 unchanged and appends filtered 2D levels down to 1x1.
[[nodiscard]] std::vector<uint8_t> generateChain(const TextureAsset& asset, std::span<const uint8_t> basePixels);

[[nodiscard]] inline uint32_t maxLevels(uint32_t width, uint32_t height)
{
    return std::bit_width(std::max(width, height));
}

[[nodiscard]] inline uint32_t extent(uint32_t base, uint32_t level) { return std::max(1u, base >> level); }

[[nodiscard]] inline uint64_t
levelBytes(uint32_t width, uint32_t height, uint32_t layers, uint32_t pixelBytes, uint32_t level)
{
    return static_cast<uint64_t>(extent(width, level)) * extent(height, level) * layers * pixelBytes;
}

[[nodiscard]] inline uint64_t
offset(uint32_t width, uint32_t height, uint32_t layers, uint32_t pixelBytes, uint32_t level)
{
    uint64_t bytes = 0;
    for (uint32_t mip = 0; mip < level; ++mip)
    {
        bytes += levelBytes(width, height, layers, pixelBytes, mip);
    }
    return bytes;
}

[[nodiscard]] inline uint64_t
totalBytes(uint32_t width, uint32_t height, uint32_t layers, uint32_t pixelBytes, uint32_t levels)
{
    return offset(width, height, layers, pixelBytes, levels);
}
}  // namespace texture_mip
