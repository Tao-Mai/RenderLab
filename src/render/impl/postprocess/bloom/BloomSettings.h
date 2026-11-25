#pragma once

#include <cstdint>

inline constexpr uint32_t kMaxBloomLevels = 6;

struct BloomSettings
{
    float    intensity    = 0.04f;

    float    scatter      = 0.7f;  // Low-resolution contribution when reconstructing the pyramid.
    float    filterRadius = 1.0f;  // Scale of the Gaussian sampling offsets.
    uint32_t levels       = 5;

    bool operator==(const BloomSettings&) const = default;

    void validate() const;
};
