#pragma once

#include "render/feature/editor/LightMarkers.h"
#include "core/profiling/Profiler.h"

#include <cstdint>
#include <functional>
#include <span>

#include <vulkan/vulkan.h>

struct EditorFrameInput
{
    bool                                   requestPick = false;
    uint32_t                               pickX       = 0;
    uint32_t                               pickY       = 0;
    std::span<const LightMarkers::DrawItem> lightDrawItems;
    std::function<DrawStatistics(VkCommandBuffer)> recordUi;
};
