#pragma once

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

// Keep the field order and stride in sync with the Slang material table.
struct alignas(16) MaterialData
{
    bool operator==(const MaterialData&) const = default;

    glm::vec4      colorFactor{1.0f};
    glm::vec4      emissiveFactor{0.0f};
    glm::vec3      specularColorFactor{1.0f};
    float          specularFactor   = 1.0f;
    float          roughness        = 1.0f;
    float          metallic         = 1.0f;
    float          alphaCutoff      = 0.5f;
    uint32_t       alphaMode        = kAlphaModeOpaque;
    float          normalScale      = 1.0f;
    uint32_t       emissiveFlags    = 0;
    uint32_t       workflow         = 0;
    float          dielectricF0     = 0.04f;
    float          glossinessFactor = 1.0f;
    NormalEncoding normalEncoding   = NormalEncoding::Rgb;
    TextureRef     colorTexture;
    TextureRef     normalTexture;
    TextureRef     metallicTexture;
    TextureRef     roughnessTexture;
    TextureRef     emissiveTexture;
    TextureRef     specularColorTexture;
    TextureRef     specularTexture;
};

static_assert(sizeof(TextureRef) == 8);
static_assert(sizeof(MaterialData) == 144);
static_assert(offsetof(MaterialData, emissiveFactor) == 16);
static_assert(offsetof(MaterialData, roughness) == 48);
static_assert(offsetof(MaterialData, normalEncoding) == 84);
static_assert(offsetof(MaterialData, colorTexture) == 88);
static_assert(offsetof(MaterialData, specularTexture) == 136);
