#pragma once

#include "core/MetadataAnnotations.h"

#include "core/Logger.h"

#include <cmath>
#include <cstdint>

inline constexpr uint32_t kMaxSvgfHistoryLength = 1024;

struct RL_RECORD SvgfSettings
{
    RL_SLIDER("historyLength", 1, kMaxSvgfHistoryLength) uint32_t historyLength = 32;
    RL_SLIDER("Alpha", 0.0, 1.0) float alpha = 0.1f;

    void validate() const
    {
        CHECK(historyLength >= 1 && historyLength <= kMaxSvgfHistoryLength);
        CHECK(std::isfinite(alpha) && alpha >= 0.0f && alpha <= 1.0f);
    }
};

#include RL_METADATA_HEADER("metadata/passes/denoise/SVGF/SvgfSettings.generated.h")
