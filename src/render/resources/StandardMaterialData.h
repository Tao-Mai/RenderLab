#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>

inline constexpr uint32_t kMaxMaterialTextures    = 1024;
inline constexpr uint32_t kMaxMaterialSamplers    = 32;
inline constexpr uint32_t kAlphaModeOpaque        = 0;
inline constexpr uint32_t kAlphaModeMask          = 1;
inline constexpr uint32_t kInvalidMaterialTexture = ~uint32_t{0};

enum class NormalEncoding : uint32_t
{
    Rgb,
    RgUnorm,
    RgSnorm,
};

struct TextureRef
{
    uint32_t textureIndex = kInvalidMaterialTexture;
    uint32_t samplerIndex = 0;

    bool operator==(const TextureRef&) const = default;
};

// Keep the field order and stride in sync with StandardMaterialData.slang.
struct alignas(16) StandardMaterialData
{
    bool operator==(const StandardMaterialData&) const = default;

    glm::vec4      baseColor{1.0f};
    glm::vec4      specularParams{0.0f, 1.0f, 0.0f, 1.0f};
    glm::vec3      emissiveColor{0.0f};
    float          emissiveFactor = 1.0f;
    glm::vec3      transmissionColor{1.0f};
    float          diffuseTransmission  = 0.0f;
    float          specularTransmission = 0.0f;
    float          indexOfRefraction    = 1.5f;
    float          normalScale          = 1.0f;
    float          alphaCutoff          = 0.5f;
    uint32_t       shadingModel         = 0;
    uint32_t       alphaMode            = kAlphaModeOpaque;
    uint32_t       doubleSided          = 0;
    uint32_t       thinSurface          = 0;
    NormalEncoding normalEncoding       = NormalEncoding::Rgb;
    uint32_t       padding              = 0;
    TextureRef     baseColorTexture;
    TextureRef     specularTexture;
    TextureRef     emissiveTexture;
    TextureRef     normalTexture;
    TextureRef     transmissionTexture;
};

static_assert(sizeof(TextureRef) == 8);
static_assert(sizeof(StandardMaterialData) == 144);
static_assert(offsetof(StandardMaterialData, specularParams) == 16);
static_assert(offsetof(StandardMaterialData, emissiveColor) == 32);
static_assert(offsetof(StandardMaterialData, transmissionColor) == 48);
static_assert(offsetof(StandardMaterialData, shadingModel) == 80);
static_assert(offsetof(StandardMaterialData, baseColorTexture) == 104);
