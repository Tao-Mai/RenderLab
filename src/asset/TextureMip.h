#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>

namespace texture_mip
{
[[nodiscard]] inline uint32_t maxLevels(uint32_t width, uint32_t height)
{
    return std::bit_width(std::max(width, height));
}

[[nodiscard]] inline uint32_t extent(uint32_t base, uint32_t level)
{
    return std::max(1u, base >> level);
}

[[nodiscard]] inline uint64_t levelBytes(
    uint32_t width, uint32_t height, uint32_t layers,
    uint32_t pixelBytes, uint32_t level)
{
    return static_cast<uint64_t>(extent(width, level)) *
        extent(height, level) * layers * pixelBytes;
}

[[nodiscard]] inline uint64_t offset(
    uint32_t width, uint32_t height, uint32_t layers,
    uint32_t pixelBytes, uint32_t level)
{
    uint64_t bytes = 0;
    for (uint32_t mip = 0; mip < level; ++mip)
    {
        bytes += levelBytes(width, height, layers, pixelBytes, mip);
    }
    return bytes;
}

[[nodiscard]] inline uint64_t totalBytes(
    uint32_t width, uint32_t height, uint32_t layers,
    uint32_t pixelBytes, uint32_t levels)
{
    return offset(width, height, layers, pixelBytes, levels);
}
}
