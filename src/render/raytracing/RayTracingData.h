#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>

struct alignas(16) RayTracingHitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint32_t firstIndex;
    uint32_t materialIndex;
    uint32_t padding[2]{};
};

static_assert(sizeof(RayTracingHitData) == 32);
static_assert(offsetof(RayTracingHitData, materialIndex) == 20);
