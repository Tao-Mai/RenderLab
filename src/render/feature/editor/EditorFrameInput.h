#pragma once

#include "core/profiling/Profiler.h"

#include <cstdint>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <span>

#include <vulkan/vulkan.h>

class GpuMesh;

struct LightMarkerDrawItem
{
    GpuMesh*  mesh;
    glm::mat4 model;
    glm::vec3 color;
    uint32_t  firstIndex;
    uint32_t  indexCount;
    uint32_t  selectionId = 0;
};

struct EditorFrameInput
{
    bool                                           requestPick = false;
    uint32_t                                       pickX       = 0;
    uint32_t                                       pickY       = 0;
    std::span<const LightMarkerDrawItem>           lightDrawItems;
    std::function<DrawStatistics(VkCommandBuffer)> recordUi;
};
