#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>

inline constexpr uint32_t kMaxRayTracingTextures = 128;
inline constexpr uint32_t kMaxRayTracingSamplers = 32;

struct RayTracingTextureRef
{
    uint32_t textureIndex = 0;
    uint32_t samplerIndex = 0;
};

// Basic metallic-roughness PBR data. No clearcoat evaluation on this route yet.
struct alignas(16) RayTracingMaterialData
{
    glm::vec4            baseColorFactor{1.0f};
    glm::vec4            emissiveFactor{0.0f};
    float                metallic          = 1.0f;
    float                roughness         = 1.0f;
    float                normalScale       = 1.0f;
    float                occlusionStrength = 1.0f;
    float                alphaCutoff       = 0.5f;
    uint32_t             alphaMode         = 0;
    uint32_t             doubleSided       = 0;
    uint32_t             padding           = 0;
    RayTracingTextureRef baseColorTexture;
    RayTracingTextureRef normalTexture;
    RayTracingTextureRef metallicTexture;
    RayTracingTextureRef roughnessTexture;
    RayTracingTextureRef emissiveTexture;
    RayTracingTextureRef occlusionTexture;
};

struct alignas(16) RayTracingHitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint32_t firstIndex;
    uint32_t materialIndex;
    uint32_t padding[2]{};
};

static_assert(sizeof(RayTracingTextureRef) == 8);
static_assert(sizeof(RayTracingMaterialData) == 112);
static_assert(offsetof(RayTracingMaterialData, metallic) == 32);
static_assert(offsetof(RayTracingMaterialData, baseColorTexture) == 64);
static_assert(offsetof(RayTracingMaterialData, occlusionTexture) == 104);
static_assert(sizeof(RayTracingHitData) == 32);
static_assert(offsetof(RayTracingHitData, materialIndex) == 20);
