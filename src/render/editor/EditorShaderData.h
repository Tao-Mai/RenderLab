#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace EditorInterface
{
inline constexpr uint32_t kViewSet            = 0;
inline constexpr uint32_t kViewUniformBinding = 0;
}  // namespace EditorInterface

struct EditorLightMarkerPushConstants
{
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

struct EditorPickingPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t  selectionId = 0;
    uint32_t  padding0    = 0;
    uint32_t  padding1    = 0;
    uint32_t  padding2    = 0;
};

struct EditorDepthPushConstants
{
    glm::mat4 model{1.0f};
};

static_assert(sizeof(EditorLightMarkerPushConstants) == 80);
static_assert(sizeof(EditorPickingPushConstants) == 80);
static_assert(sizeof(EditorDepthPushConstants) == 64);
