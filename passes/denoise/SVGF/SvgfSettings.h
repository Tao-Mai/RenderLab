#pragma once

#include "core/Logger.h"
#include "render/graph/PassSettings.h"

#include <cmath>
#include <cstdint>

inline constexpr uint32_t kMaxSvgfHistoryLength = 1024;

struct SvgfSettings
{
    [[= PassSliderRange{1, kMaxSvgfHistoryLength}]] uint32_t historyLength = 32;
    [[= PassSliderRange{0.0, 1.0}]] float emaAlpha = 0.1f;

    void validate() const
    {
        CHECK(historyLength >= 1 && historyLength <= kMaxSvgfHistoryLength);
        CHECK(std::isfinite(emaAlpha) && emaAlpha >= 0.0f && emaAlpha <= 1.0f);
    }
};
