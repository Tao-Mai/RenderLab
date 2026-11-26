#include "asset/import/GltfMaterialExtensions.h"

#include "asset/import/MaterialTexture.h"
#include "core/Logger.h"

#include <cmath>
#include <limits>
#include <tiny_gltf.h>

namespace
{
constexpr float kMaxValue = std::numeric_limits<float>::max();

struct ExtensionReader
{
    const tinygltf::Value&       value;
    const std::filesystem::path& source;
    const GltfTextureLoader&     loadTexture;

    float number(const char* name, float fallback, float minimum = 0.0f, float maximum = kMaxValue) const
    {
        if (!value.Has(name))
            return fallback;
        const auto& field = value.Get(name);
        CHECK(field.IsNumber(), "invalid material field '{}' in '{}'", name, source.string());
        const float result = static_cast<float>(field.GetNumberAsDouble());
        CHECK(
            std::isfinite(result) && result >= minimum && result <= maximum,
            "invalid material '{}' value {} in '{}'",
            name,
            result,
            source.string()
        );
        return result;
    }

    glm::vec3 color(const char* name, glm::vec3 fallback, float maximum = 1.0f) const
    {
        if (!value.Has(name))
            return fallback;
        const auto& field = value.Get(name);
        CHECK(field.IsArray() && field.ArrayLen() == 3, "invalid material color '{}' in '{}'", name, source.string());
        glm::vec3 result;
        for (int channel = 0; channel < 3; ++channel)
        {
            CHECK(field.Get(channel).IsNumber(), "invalid material color '{}' in '{}'", name, source.string());
            result[channel] = static_cast<float>(field.Get(channel).GetNumberAsDouble());
            CHECK(
                std::isfinite(result[channel]) && result[channel] >= 0.0f && result[channel] <= maximum,
                "invalid material color '{}' in '{}'",
                name,
                source.string()
            );
        }
        return result;
    }

    std::optional<TextureBinding> texture(const char* name, ColorSpace colorSpace = ColorSpace::Linear) const
    {
        if (!value.Has(name))
            return std::nullopt;
        const auto& info = value.Get(name);
        CHECK(
            info.IsObject() && info.Has("index") && info.Get("index").IsInt(),
            "invalid material texture '{}' in '{}'",
            name,
            source.string()
        );
        CHECK(
            !info.Has("texCoord") || (info.Get("texCoord").IsInt() && info.Get("texCoord").GetNumberAsInt() == 0),
            "material texture '{}' requires unsupported UV coordinates in '{}'",
            name,
            source.string()
        );
        CHECK(
            !info.Has("extensions") || !info.Get("extensions").Has("KHR_texture_transform"),
            "material texture '{}' requires unsupported texture transforms in '{}'",
            name,
            source.string()
        );
        const auto result = loadTexture(info.Get("index").GetNumberAsInt(), colorSpace);
        CHECK(result, "invalid material texture '{}' in '{}'", name, source.string());
        return result;
    }
};
}  // namespace

void importMaterialExtensions(
    StandardMaterial&            material,
    const tinygltf::Material&    source,
    const std::filesystem::path& sourcePath,
    const GltfTextureLoader&     loadTexture
)
{
    const auto read = [&](const char* name, auto&& apply)
    {
        const auto found = source.extensions.find(name);
        if (found == source.extensions.end())
            return;
        CHECK(found->second.IsObject(), "invalid material extension '{}' in '{}'", name, sourcePath.string());
        apply(ExtensionReader{found->second, sourcePath, loadTexture});
    };

    read(
        "KHR_materials_ior",
        [&](const ExtensionReader& reader)
        {
            const float ior = reader.number("ior", 1.5f);
            CHECK(ior == 0.0f || ior >= 1.0f);
            material.indexOfRefraction = ior == 0.0f ? kMaxValue : ior;
        }
    );
    read(
        "KHR_materials_pbrSpecularGlossiness",
        [&](const ExtensionReader& reader)
        {
            CHECK(
                !source.extensions.contains("KHR_materials_specular") &&
                    !source.extensions.contains("KHR_materials_ior"),
                "SG cannot be combined with Specular or IOR in '{}'",
                sourcePath.string()
            );
            material.shadingModel = ShadingModel::SpecGloss;
            material.baseColor    = glm::vec4{1.0f};
            if (reader.value.Has("diffuseFactor"))
            {
                const auto& factor = reader.value.Get("diffuseFactor");
                CHECK(
                    factor.IsArray() && factor.ArrayLen() == 4,
                    "invalid SG diffuseFactor in '{}'",
                    sourcePath.string()
                );
                for (int channel = 0; channel < 4; ++channel)
                {
                    CHECK(factor.Get(channel).IsNumber(), "invalid SG diffuseFactor in '{}'", sourcePath.string());
                    material.baseColor[channel] = static_cast<float>(factor.Get(channel).GetNumberAsDouble());
                }
            }
            material.specularParams = glm::vec4{
                reader.color("specularFactor", glm::vec3{1.0f}),
                reader.number("glossinessFactor", 1.0f, 0.0f, 1.0f)
            };
            material.validate();
            material.baseColorTexture = asset_import::bakeMaterialTexture(
                reader.texture("diffuseTexture", ColorSpace::Srgb),
                material.baseColor
            );
            material.specularTexture = asset_import::bakeMaterialTexture(
                reader.texture("specularGlossinessTexture", ColorSpace::Srgb),
                material.specularParams
            );
        }
    );
    read(
        "KHR_materials_specular",
        [&](const ExtensionReader& reader)
        {
            asset_import::convertToSpecGloss(
                material,
                reader.number("specularFactor", 1.0f, 0.0f, 1.0f),
                reader.color("specularColorFactor", glm::vec3{1.0f}, kMaxValue),
                reader.texture("specularTexture"),
                reader.texture("specularColorTexture", ColorSpace::Srgb)
            );
        }
    );
    read(
        "KHR_materials_transmission",
        [&](const ExtensionReader& reader)
        {
            material.specularTransmission = reader.number("transmissionFactor", 0.0f, 0.0f, 1.0f);
            // StandardMaterial has one transmission color slot and uniform lobe blend weights.
            const auto& pbr              = source.pbrMetallicRoughness;
            material.transmissionColor   = {pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2]};
            material.transmissionTexture = pbr.baseColorTexture.index < 0
                ? std::nullopt
                : loadTexture(pbr.baseColorTexture.index, ColorSpace::Srgb);
            material.transmissionTexture = asset_import::bakeMaterialTexture(
                material.transmissionTexture,
                glm::vec4{material.transmissionColor, 1.0f}
            );
            if (reader.value.Has("transmissionTexture"))
                LOG_WARNING(
                    "StandardMaterial uses uniform transmission; ignoring transmission mask in '{}'",
                    sourcePath.string()
                );
        }
    );
    read(
        "KHR_materials_diffuse_transmission",
        [&](const ExtensionReader& reader)
        {
            material.diffuseTransmission = reader.number("diffuseTransmissionFactor", 0.0f, 0.0f, 1.0f);
            material.transmissionColor   = reader.color("diffuseTransmissionColorFactor", glm::vec3{1.0f});
            material.transmissionTexture = asset_import::bakeMaterialTexture(
                reader.texture("diffuseTransmissionColorTexture", ColorSpace::Srgb),
                glm::vec4{material.transmissionColor, 1.0f}
            );
            if (reader.value.Has("diffuseTransmissionTexture"))
                LOG_WARNING(
                    "StandardMaterial uses uniform diffuse transmission; ignoring transmission mask in '{}'",
                    sourcePath.string()
                );
        }
    );
    read(
        "KHR_materials_volume",
        [&](const ExtensionReader& reader)
        {
            material.thinSurface = reader.number("thicknessFactor", 0.0f) == 0.0f;
            if (reader.value.Has("attenuationDistance") || reader.value.Has("attenuationColor") ||
                reader.value.Has("thicknessTexture"))
                LOG_WARNING(
                    "StandardMaterial surface import does not model glTF volume attenuation or thickness textures: {}",
                    sourcePath.string()
                );
        }
    );
    read(
        "KHR_materials_emissive_strength",
        [&](const ExtensionReader& reader) { material.emissiveFactor = reader.number("emissiveStrength", 1.0f); }
    );
    for (const char* name :
         {"KHR_materials_clearcoat",
          "KHR_materials_sheen",
          "KHR_materials_anisotropy",
          "KHR_materials_iridescence",
          "KHR_materials_dispersion",
          "KHR_materials_unlit"})
        if (source.extensions.contains(name))
            LOG_WARNING("StandardMaterial does not support material extension '{}' in '{}'", name, sourcePath.string());
}
