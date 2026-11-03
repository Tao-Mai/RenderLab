#pragma once

#include "render/raytracing/RayTracingBindings.h"

#include "render/resources/ShaderData.h"

#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

inline constexpr uint32_t kInvalidEmissiveTriangle = ~0u;

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
static_assert(offsetof(PathTracingParameters, padding) == 44);

struct alignas(16) RayTracingHitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint32_t firstIndex;
    uint32_t materialIndex;
    uint32_t firstEmissiveTriangle = kInvalidEmissiveTriangle;
    uint32_t padding               = 0;
};

static_assert(sizeof(RayTracingHitData) == 32);
static_assert(offsetof(RayTracingHitData, materialIndex) == 20);
static_assert(offsetof(RayTracingHitData, firstEmissiveTriangle) == 24);

struct alignas(16) EmissiveTriangleTask
{
    glm::mat4 model;
    uint32_t  objectIndex;
    uint32_t  firstTriangle;
    uint32_t  triangleCount;
    uint32_t  padding = 0;
};
static_assert(sizeof(EmissiveTriangleTask) == 80);
static_assert(offsetof(EmissiveTriangleTask, objectIndex) == 64);

struct alignas(16) EmissiveTriangleHeader
{
    uint32_t triangleCount;
    uint32_t padding[3]{};
};
static_assert(sizeof(EmissiveTriangleHeader) == 16);

struct alignas(16) EmissiveTriangle
{
    glm::vec3  position;
    uint32_t   materialIndex;
    glm::vec3  edge1;
    float      area;
    glm::vec3  edge2;
    uint32_t   padding = 0;
    glm::vec2  uv0;
    glm::vec2  uv1;
    glm::vec2  uv2;
    glm::uvec2 uvPadding{};
};
static_assert(sizeof(EmissiveTriangle) == 80);
static_assert(offsetof(EmissiveTriangle, materialIndex) == 12);
static_assert(offsetof(EmissiveTriangle, area) == 28);
static_assert(offsetof(EmissiveTriangle, uv0) == 48);
