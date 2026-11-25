#pragma once

#include "core/MetadataAnnotations.h"

#include <cstdint>

inline constexpr uint32_t kMaxBloomLevels = 6;

struct RL_RECORD BloomSettings
{
    RL_FIELD("intensity") float    intensity    = 0.04f;
    RL_FIELD("scatter") float    scatter      = 0.7f;  // Low-resolution contribution when reconstructing the pyramid.
    RL_FIELD("filterRadius") float    filterRadius = 1.0f;  // Scale of the Gaussian sampling offsets.
    RL_FIELD("levels") uint32_t levels       = 5;

    void validate() const;
};
