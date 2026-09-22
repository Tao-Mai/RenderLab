#pragma once

#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

struct MeshPushConstants
{
    glm::mat4 model{1.0f};
};

struct LightPushConstants
{
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

struct EditorPickingPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t selectionId = 0;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};

static_assert(sizeof(MeshPushConstants) == 64);
static_assert(sizeof(LightPushConstants) == 80);
static_assert(sizeof(EditorPickingPushConstants) == 80);
