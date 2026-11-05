#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>

inline constexpr uint32_t kMaxMaterialTextures = 1024;
inline constexpr uint32_t kMaxMaterialSamplers = 32;
inline constexpr uint32_t kAlphaModeOpaque     = 0;
inline constexpr uint32_t kAlphaModeMask       = 1;
inline constexpr uint32_t kAlphaModeBlend      = 2;

enum class NormalEncoding : uint32_t
{
    Rgb,
    RgUnorm,
    RgSnorm,
};

struct TextureRef
{
    uint32_t textureIndex = 0;
    uint32_t samplerIndex = 0;

    bool operator==(const TextureRef&) const = default;
};

// glTF material SSBO used by rasterization and realtime ray tracing.
struct alignas(16) MaterialData
{
    bool operator==(const MaterialData&) const = default;

    glm::vec4               colorFactor{1.0f};
    float                   roughness         = 1.0f;
    float                   metallic          = 1.0f;
    float                   alphaCutoff       = 0.5f;
    uint32_t                alphaMode         = kAlphaModeOpaque;
    float                   normalScale       = 1.0f;
    float                   occlusionStrength = 1.0f;
    uint32_t                doubleSided       = 0;
    uint32_t                workflow          = 0;
    glm::vec4               emissiveFactor{0.0f};
    glm::vec3               sheenColorFactor{0.0f};
    float                   sheenRoughnessFactor     = 0.0f;
    float                   clearcoatFactor          = 0.0f;
    float                   clearcoatRoughnessFactor = 0.0f;
    float                   clearcoatNormalScale     = 1.0f;
    float                   clearcoatPadding         = 0.0f;
    glm::vec3               specularColorFactor{1.0f};
    float                   specularFactor   = 1.0f;
    float                   dielectricF0     = 0.04f;
    float                   glossinessFactor = 1.0f;
    NormalEncoding          normalEncoding = NormalEncoding::Rgb;
    NormalEncoding          clearcoatNormalEncoding = NormalEncoding::Rgb;
    TextureRef              colorTexture;
    TextureRef              normalTexture;
    TextureRef              metallicTexture;
    TextureRef              roughnessTexture;
    TextureRef              emissiveTexture;
    TextureRef              occlusionTexture;
    TextureRef              sheenColorTexture;
    TextureRef              sheenRoughnessTexture;
    TextureRef              clearcoatTexture;
    TextureRef              clearcoatRoughnessTexture;
    TextureRef              clearcoatNormalTexture;
    TextureRef              specularColorTexture;
    TextureRef              specularTexture;
    std::array<uint32_t, 2> texturePadding{};
};

static_assert(sizeof(TextureRef) == 8);
static_assert(sizeof(MaterialData) == 240);
static_assert(offsetof(MaterialData, roughness) == 16);
static_assert(offsetof(MaterialData, emissiveFactor) == 48);
static_assert(offsetof(MaterialData, sheenColorFactor) == 64);
static_assert(offsetof(MaterialData, clearcoatFactor) == 80);
static_assert(offsetof(MaterialData, specularColorFactor) == 96);
static_assert(offsetof(MaterialData, dielectricF0) == 112);
static_assert(offsetof(MaterialData, normalEncoding) == 120);
static_assert(offsetof(MaterialData, colorTexture) == 128);
static_assert(offsetof(MaterialData, specularTexture) == 224);
static_assert(offsetof(MaterialData, texturePadding) == 232);
