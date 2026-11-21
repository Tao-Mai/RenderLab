#pragma once

#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/graph/PassInputs.h"
#include "render/raytracing/PathTracingParameters.h"

#include <array>

inline constexpr std::array kPathTracingMissEntries{"missMain", "visibilityMissMain"};
inline constexpr std::array kPathTracingHitEntries{"closestHitMain", "lightClosestHitMain"};

[[nodiscard]] inline PathTracingParameters makePathTracingParameters(
    const PassFrame& frame, uint32_t maxBounces, uint32_t sampleIndex
)
{
    const auto& viewport = frame.view.viewport;
    return {
        .viewport = {viewport.x, viewport.y, viewport.width, viewport.height},
        .lightRanges = frame.renderData.lightRanges,
        .maxBounces = maxBounces,
        .sampleIndex = sampleIndex,
        .iblEnabled = frame.setup.config.feature.ibl && frame.renderData.environmentMap ? 1u : 0u,
    };
}
