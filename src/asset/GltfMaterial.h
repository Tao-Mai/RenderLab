#pragma once

#include "asset/Asset.h"
#include "asset/TextureBinding.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <cstdint>

enum class MaterialWorkflow : uint32_t
{
    MetallicRoughness,
    SpecularGlossiness,
};

struct GltfMaterial
{
    using ID = AssetID<GltfMaterial>;
    static constexpr std::string_view dir = "Material";

    ID id;

    struct SpecularGlossiness
    {
        glm::vec4                     diffuseFactor{1.0f};
        glm::vec3                     specularFactor{1.0f};
        float                         glossinessFactor = 1.0f;
        std::optional<TextureBinding> diffuseTexture;
        std::optional<TextureBinding> specularGlossinessTexture;
    };

    struct Clearcoat
    {
        float                         factor          = 0.0f;
        float                         roughnessFactor = 0.0f;
        float                         normalScale     = 1.0f;
        
        std::optional<TextureBinding> texture;
        std::optional<TextureBinding> roughnessTexture;
        std::optional<TextureBinding> normalTexture;
    };

    struct Sheen
    {
        glm::vec3                     colorFactor{0.0f};
        float                         roughnessFactor = 0.0f;
        
        std::optional<TextureBinding> colorTexture;
        std::optional<TextureBinding> roughnessTexture;
    };

    struct Anisotropy
    {
        float                         strength = 0.0f;
        float                         rotation = 0.0f; // Radians in tangent space.
        std::optional<TextureBinding> texture;
    };

    struct Transmission
    {
        float                         factor = 0.0f;
        std::optional<TextureBinding> texture;
    };

    struct Volume
    {
        float                         thicknessFactor = 0.0f;
        std::optional<TextureBinding> thicknessTexture;
        std::optional<float>          attenuationDistance; // null means no attenuation (infinite distance).
        glm::vec3                     attenuationColor{1.0f};
    };

    struct Specular
    {
        float                         factor = 1.0f;
        glm::vec3                     colorFactor{1.0f};
        std::optional<TextureBinding> texture;
        std::optional<TextureBinding> colorTexture;
    };

    struct Iridescence
    {
        float                         factor = 0.0f;
        float                         ior = 1.3f;
        float                         thicknessMinimum = 100.0f; // Nanometers.
        float                         thicknessMaximum = 400.0f;

        std::optional<TextureBinding> texture;
        std::optional<TextureBinding> thicknessTexture;
    };

    struct DiffuseTransmission
    {
        float                         factor = 0.0f;
        glm::vec3                     colorFactor{1.0f};
        std::optional<TextureBinding> texture;
        std::optional<TextureBinding> colorTexture;
    };

    MaterialWorkflow workflow;

    std::optional<SpecularGlossiness>   specularGlossiness;

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
