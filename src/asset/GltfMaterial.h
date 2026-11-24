#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/MaterialAsset.h"
#include "asset/TextureBinding.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <cstdint>


enum class RL_ENUM MaterialWorkflow : uint32_t
{
    MetallicRoughness RL_ENUM_VALUE("MetallicRoughness"),
    SpecularGlossiness RL_ENUM_VALUE("SpecularGlossiness"),
};

struct RL_REGISTER_TYPE(MaterialAsset, "GltfMaterial") GltfMaterial : MaterialAsset
{
    struct RL_RECORD SpecularGlossiness
    {
        RL_FIELD("diffuseFactor") glm::vec4                     diffuseFactor{1.0f};
        RL_FIELD("specularFactor") glm::vec3                     specularFactor{1.0f};
        RL_FIELD("glossinessFactor") float                         glossinessFactor = 1.0f;
        RL_FIELD("diffuseTexture") std::optional<TextureBinding> diffuseTexture;
        RL_FIELD("specularGlossinessTexture") std::optional<TextureBinding> specularGlossinessTexture;

        bool operator==(const SpecularGlossiness&) const = default;
    };

    struct RL_RECORD Clearcoat
    {
        RL_FIELD("factor") float                         factor          = 0.0f;
        RL_FIELD("roughnessFactor") float                         roughnessFactor = 0.0f;
        RL_FIELD("normalScale") float                         normalScale     = 1.0f;
        RL_FIELD("texture") std::optional<TextureBinding> texture;
        RL_FIELD("roughnessTexture") std::optional<TextureBinding> roughnessTexture;
        RL_FIELD("normalTexture") std::optional<TextureBinding> normalTexture;

        bool operator==(const Clearcoat&) const = default;
    };

    struct RL_RECORD Sheen
    {
        RL_FIELD("colorFactor") glm::vec3                     colorFactor{0.0f};
        RL_FIELD("roughnessFactor") float                         roughnessFactor = 0.0f;
        RL_FIELD("colorTexture") std::optional<TextureBinding> colorTexture;
        RL_FIELD("roughnessTexture") std::optional<TextureBinding> roughnessTexture;

        bool operator==(const Sheen&) const = default;
    };

    struct RL_RECORD Anisotropy
    {
        RL_FIELD("strength") float                         strength = 0.0f;
        RL_FIELD("rotation") float                         rotation = 0.0f; // Radians in tangent space.
        RL_FIELD("texture") std::optional<TextureBinding> texture;

        bool operator==(const Anisotropy&) const = default;
    };

    struct RL_RECORD Transmission
    {
        RL_FIELD("factor") float                         factor = 0.0f;
        RL_FIELD("texture") std::optional<TextureBinding> texture;

        bool operator==(const Transmission&) const = default;
    };

    struct RL_RECORD Volume
    {
        RL_FIELD("thicknessFactor") float                         thicknessFactor = 0.0f;
        RL_FIELD("thicknessTexture") std::optional<TextureBinding> thicknessTexture;
        RL_FIELD("attenuationDistance") std::optional<float>          attenuationDistance; // null means no attenuation (infinite distance).
        RL_FIELD("attenuationColor") glm::vec3                     attenuationColor{1.0f};

        bool operator==(const Volume&) const = default;
    };

    struct RL_RECORD Specular
    {
        RL_FIELD("factor") float                         factor = 1.0f;
        RL_FIELD("colorFactor") glm::vec3                     colorFactor{1.0f};
        RL_FIELD("texture") std::optional<TextureBinding> texture;
        RL_FIELD("colorTexture") std::optional<TextureBinding> colorTexture;

        bool operator==(const Specular&) const = default;
    };

    struct RL_RECORD Iridescence
    {
        RL_FIELD("factor") float                         factor = 0.0f;
        RL_FIELD("ior") float                         ior = 1.3f;
        RL_FIELD("thicknessMinimum") float                         thicknessMinimum = 100.0f; // Nanometers.
        RL_FIELD("thicknessMaximum") float                         thicknessMaximum = 400.0f;
        RL_FIELD("texture") std::optional<TextureBinding> texture;
        RL_FIELD("thicknessTexture") std::optional<TextureBinding> thicknessTexture;

        bool operator==(const Iridescence&) const = default;
    };

    struct RL_RECORD DiffuseTransmission
    {
        RL_FIELD("factor") float                         factor = 0.0f;
        RL_FIELD("colorFactor") glm::vec3                     colorFactor{1.0f};
        RL_FIELD("texture") std::optional<TextureBinding> texture;
        RL_FIELD("colorTexture") std::optional<TextureBinding> colorTexture;

        bool operator==(const DiffuseTransmission&) const = default;
    };

    bool operator==(const GltfMaterial&) const = default;

    RL_FIELD("workflow") std::optional<MaterialWorkflow>     workflow;
    RL_FIELD("specularGlossiness") std::optional<SpecularGlossiness>   specularGlossiness;
    RL_FIELD("baseColorFactor") std::optional<glm::vec4>            baseColorFactor;
    RL_FIELD("metallic") std::optional<float>                metallic;
    RL_FIELD("roughness") std::optional<float>                roughness;
    RL_FIELD("ao") std::optional<float>                ao;
    RL_FIELD("emissive") std::optional<glm::vec3>            emissive;
    RL_FIELD("normalScale") std::optional<float>                normalScale;
    RL_FIELD("baseColorTexture") std::optional<TextureBinding>       baseColorTexture;
    RL_FIELD("normalTexture") std::optional<TextureBinding>       normalTexture;
    RL_FIELD("metallicTexture") std::optional<TextureBinding>       metallicTexture;
    RL_FIELD("roughnessTexture") std::optional<TextureBinding>       roughnessTexture;
    RL_FIELD("aoTexture") std::optional<TextureBinding>       aoTexture;
    RL_FIELD("emissiveTexture") std::optional<TextureBinding>       emissiveTexture;
    RL_FIELD("alphaMode") std::optional<std::string>          alphaMode;
    RL_FIELD("alphaCutoff") std::optional<float>                alphaCutoff;
    RL_FIELD("doubleSided") std::optional<bool>                 doubleSided;
    RL_FIELD("clearcoat") std::optional<Clearcoat>            clearcoat;
    RL_FIELD("sheen") std::optional<Sheen>                sheen;
    RL_FIELD("anisotropy") std::optional<Anisotropy>           anisotropy;
    RL_FIELD("transmission") std::optional<Transmission>         transmission;
    RL_FIELD("volume") std::optional<Volume>               volume;
    RL_FIELD("ior") std::optional<float>                ior;
    RL_FIELD("specular") std::optional<Specular>             specular;
    RL_FIELD("iridescence") std::optional<Iridescence>          iridescence;
    RL_FIELD("dispersion") std::optional<float>                dispersion;
    RL_FIELD("emissiveStrength") std::optional<float>                emissiveStrength;
    RL_FIELD("unlit") std::optional<bool>                 unlit;
    RL_FIELD("diffuseTransmission") std::optional<DiffuseTransmission>  diffuseTransmission;
};

using MaterialOverride = GltfMaterial;
