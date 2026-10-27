#pragma once

#include "render/resources/ShaderData.h"

#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>

inline constexpr uint32_t kRayPipelineRecursionDepth = 1;

struct alignas(16) PathTracingParameters
{
    glm::vec4   viewport;
    LightRanges lightRanges;
    uint32_t    maxBounces;
    uint32_t    sampleIndex;
    uint32_t    iblEnabled;
    uint32_t    padding = 0;
};
static_assert(sizeof(PathTracingParameters) == 48);
static_assert(offsetof(PathTracingParameters, lightRanges) == 16);
static_assert(offsetof(PathTracingParameters, maxBounces) == 32);
static_assert(offsetof(PathTracingParameters, sampleIndex) == 36);
static_assert(offsetof(PathTracingParameters, iblEnabled) == 40);

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
