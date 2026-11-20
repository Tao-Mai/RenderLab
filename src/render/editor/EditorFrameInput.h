#pragma once

#include <cstdint>
#include <functional>

#include <vulkan/vulkan.h>

struct EditorFrameInput
{
    bool     requestPick = false;
    uint32_t pickX       = 0;
    uint32_t pickY       = 0;
    std::function<void(VkCommandBuffer)> recordUi;
};
