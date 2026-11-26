#pragma once

#include "asset/Asset.h"
#include "asset/TextureBinding.h"

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>

enum class ShadingModel : uint32_t
{
    MetalRough,
    SpecGloss,
};

enum class AlphaMode : uint32_t
{
    Opaque,
    Mask,
};

// Texture slots replace uniform values, as in Falcor's StandardMaterial.
struct StandardMaterial
{
    using ID                              = AssetID<StandardMaterial>;
    static constexpr std::string_view dir = "Material";

    ID           id;
    ShadingModel shadingModel = ShadingModel::MetalRough;
    glm::vec4    baseColor{1.0f};

    // MR: occlusion, roughness, metallic; SG: specular RGB, glossiness.
    glm::vec4 specularParams{0.0f, 1.0f, 0.0f, 1.0f};
    glm::vec3 emissiveColor{0.0f};
    float     emissiveFactor = 1.0f;
    glm::vec3 transmissionColor{1.0f};
    float     diffuseTransmission  = 0.0f;
    float     specularTransmission = 0.0f;
    float     indexOfRefraction    = 1.5f;
    float     normalScale          = 1.0f;
    AlphaMode alphaMode            = AlphaMode::Opaque;
    float     alphaCutoff          = 0.5f;
    bool      doubleSided          = false;
    bool      thinSurface          = false;

    std::optional<TextureBinding> baseColorTexture;
    std::optional<TextureBinding> specularTexture;
    std::optional<TextureBinding> emissiveTexture;
    std::optional<TextureBinding> normalTexture;
    std::optional<TextureBinding> transmissionTexture;

    void validate() const;
};
