#pragma once

#include "asset/MaterialAsset.h"
#include "asset/TextureBinding.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>

struct ShaderAsset;

struct GltfMaterial : MaterialAsset
{
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

    struct Sheen
    {
        glm::vec3                     colorFactor{0.0f};
        float                         roughnessFactor = 0.0f;
        std::optional<TextureBinding> colorTexture;
        std::optional<TextureBinding> roughnessTexture;

        bool operator==(const Sheen&) const = default;
    };

    struct Anisotropy
    {
        float                         strength = 0.0f;
        float                         rotation = 0.0f; // Radians in tangent space.
        std::optional<TextureBinding> texture;

        bool operator==(const Anisotropy&) const = default;
    };

    struct Transmission
    {
        float                         factor = 0.0f;
        std::optional<TextureBinding> texture;

        bool operator==(const Transmission&) const = default;
    };

    struct Volume
    {
        float                         thicknessFactor = 0.0f;
        std::optional<TextureBinding> thicknessTexture;
        std::optional<float>          attenuationDistance; // null means no attenuation (infinite distance).
        glm::vec3                     attenuationColor{1.0f};

        bool operator==(const Volume&) const = default;
    };

    struct Specular
    {
        float                         factor = 1.0f;
        glm::vec3                     colorFactor{1.0f};
        std::optional<TextureBinding> texture;
        std::optional<TextureBinding> colorTexture;

        bool operator==(const Specular&) const = default;
    };

    struct Iridescence
    {
        float                         factor = 0.0f;
        float                         ior = 1.3f;
        float                         thicknessMinimum = 100.0f; // Nanometers.
        float                         thicknessMaximum = 400.0f;
        std::optional<TextureBinding> texture;
        std::optional<TextureBinding> thicknessTexture;

        bool operator==(const Iridescence&) const = default;
    };

    struct DiffuseTransmission
    {
        float                         factor = 0.0f;
        glm::vec3                     colorFactor{1.0f};
        std::optional<TextureBinding> texture;
        std::optional<TextureBinding> colorTexture;

        bool operator==(const DiffuseTransmission&) const = default;
    };

    bool operator==(const GltfMaterial&) const = default;

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
    std::optional<Sheen>                sheen;
    std::optional<Anisotropy>           anisotropy;
    std::optional<Transmission>         transmission;
    std::optional<Volume>               volume;
    std::optional<float>                ior;
    std::optional<Specular>             specular;
    std::optional<Iridescence>          iridescence;
    std::optional<float>                dispersion;
    std::optional<float>                emissiveStrength;
    std::optional<bool>                 unlit;
    std::optional<DiffuseTransmission>  diffuseTransmission;
};

using MaterialOverride = GltfMaterial;
