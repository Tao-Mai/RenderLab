#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

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
