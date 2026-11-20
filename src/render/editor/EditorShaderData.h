#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>

namespace EditorInterface
{
inline constexpr uint32_t kViewSet            = 0;
inline constexpr uint32_t kViewUniformBinding = 0;
}  // namespace EditorInterface

struct EditorPickingPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t  selectionId = 0;
    uint32_t  padding0    = 0;
    uint32_t  padding1    = 0;
    uint32_t  padding2    = 0;
};

static_assert(sizeof(EditorPickingPushConstants) == 80);
