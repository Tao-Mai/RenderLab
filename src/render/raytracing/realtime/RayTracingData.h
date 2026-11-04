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
    uint32_t    candidateCount;
};
static_assert(sizeof(PathTracingParameters) == 48);
static_assert(offsetof(PathTracingParameters, lightRanges) == 16);
static_assert(offsetof(PathTracingParameters, maxBounces) == 32);
static_assert(offsetof(PathTracingParameters, sampleIndex) == 36);
static_assert(offsetof(PathTracingParameters, iblEnabled) == 40);
static_assert(offsetof(PathTracingParameters, candidateCount) == 44);

struct alignas(16) RestirFrameParameters
{
    glm::mat4 previousViewProjection{1.0f};
    glm::vec4 previousViewport{};
    glm::vec4 previousDepthRow{};
    uint32_t  width;
    uint32_t  height;
    uint32_t  writeLayer;
    uint32_t  historyValid;
    uint32_t  maxHistoryLength;
    uint32_t  spatialSampleCount;
    float     spatialRadius;
    uint32_t  temporalReuse;
    uint32_t  spatialReuse;
    uint32_t  padding[3]{};
};
static_assert(sizeof(RestirFrameParameters) == 144);
static_assert(offsetof(RestirFrameParameters, width) == 96);
static_assert(offsetof(RestirFrameParameters, maxHistoryLength) == 112);
static_assert(offsetof(RestirFrameParameters, spatialReuse) == 128);
static_assert(offsetof(RestirFrameParameters, padding) == 132);

// Explicit rows match the shader's structured-buffer layout.
struct alignas(16) RestirSurfaceData
{
    glm::vec4  positionDepth;
    glm::uvec4 directions;
    glm::uvec4 material;
    glm::uvec4 metadata;
};
static_assert(sizeof(RestirSurfaceData) == 64);

struct alignas(16) RestirReservoirData
{
    uint32_t  lightKind;
    uint32_t  lightIndex;
    glm::vec2 uv;
    float     weightSum;
    float     target;
    uint32_t  sampleCount;
    uint32_t  padding;
};
static_assert(sizeof(RestirReservoirData) == 32);
static_assert(offsetof(RestirReservoirData, uv) == 8);
static_assert(offsetof(RestirReservoirData, weightSum) == 16);
static_assert(offsetof(RestirReservoirData, sampleCount) == 24);

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
