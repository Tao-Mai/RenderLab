#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

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
    uint32_t  candidateCount;
    uint32_t  padding[2]{};
};
static_assert(sizeof(RestirFrameParameters) == 144);
static_assert(offsetof(RestirFrameParameters, width) == 96);
static_assert(offsetof(RestirFrameParameters, maxHistoryLength) == 112);
static_assert(offsetof(RestirFrameParameters, spatialReuse) == 128);
static_assert(offsetof(RestirFrameParameters, candidateCount) == 132);
static_assert(offsetof(RestirFrameParameters, padding) == 136);

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
