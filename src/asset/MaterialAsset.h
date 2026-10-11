#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include "asset/Asset.h"
#include "asset/TextureBinding.h"
struct ShaderAsset;

struct MaterialAsset : Asset
{
    using ID                              = AssetID<MaterialAsset>;
    static constexpr std::string_view dir = "Material";

    struct Clearcoat
    {
        float                         factor          = 0.0f;
        float                         roughnessFactor = 0.0f;
        float                         normalScale     = 1.0f;
        std::optional<TextureBinding> texture;
        std::optional<TextureBinding> roughnessTexture;
        std::optional<TextureBinding> normalTexture;

        bool operator==(const Clearcoat&) const = default;
    };

    bool operator==(const MaterialAsset&) const = default;

    ID                                  id;
    std::optional<AssetID<ShaderAsset>> shaderId;
    std::optional<glm::vec4>            baseColorFactor;
    std::optional<float>                metallic;
    std::optional<float>                roughness;
    std::optional<float>                ao;
    std::optional<glm::vec3>            emissive;
    std::optional<float>                normalScale;
    std::optional<TextureBinding>       baseColorTexture;
    std::optional<TextureBinding>       normalTexture;
    std::optional<TextureBinding>       metallicTexture;
    std::optional<TextureBinding>       roughnessTexture;
    std::optional<TextureBinding>       aoTexture;
    std::optional<TextureBinding>       emissiveTexture;
    std::optional<std::string>          alphaMode;
    std::optional<float>                alphaCutoff;
    std::optional<bool>                 doubleSided;
    std::optional<Clearcoat>            clearcoat;
};

using MaterialOverride = MaterialAsset;
