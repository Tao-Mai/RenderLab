#include "asset/import/GltfMaterialExtensions.h"

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
    GltfMaterial&            material,
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
        "KHR_materials_clearcoat",
        [&](const ExtensionReader& reader)
        {
            auto& layer            = material.clearcoat.emplace();
            layer.factor           = reader.number("clearcoatFactor", layer.factor, 0.0f, 1.0f);
            layer.roughnessFactor  = reader.number("clearcoatRoughnessFactor", layer.roughnessFactor, 0.0f, 1.0f);
            layer.texture          = reader.texture("clearcoatTexture");
            layer.roughnessTexture = reader.texture("clearcoatRoughnessTexture");
            layer.normalTexture    = reader.texture("clearcoatNormalTexture");
            if (reader.value.Has("clearcoatNormalTexture"))
            {
                const ExtensionReader normal{reader.value.Get("clearcoatNormalTexture"), sourcePath, loadTexture};
                layer.normalScale = normal.number("scale", layer.normalScale);
            }
        }
    );
    read(
        "KHR_materials_sheen",
        [&](const ExtensionReader& reader)
        {
            auto& layer            = material.sheen.emplace();
            layer.colorFactor      = reader.color("sheenColorFactor", layer.colorFactor);
            layer.roughnessFactor  = reader.number("sheenRoughnessFactor", layer.roughnessFactor, 0.0f, 1.0f);
            layer.colorTexture     = reader.texture("sheenColorTexture", ColorSpace::Srgb);
            layer.roughnessTexture = reader.texture("sheenRoughnessTexture");
        }
    );
    read(
        "KHR_materials_anisotropy",
        [&](const ExtensionReader& reader)
        {
            auto& layer    = material.anisotropy.emplace();
            layer.strength = reader.number("anisotropyStrength", layer.strength, 0.0f, 1.0f);
            layer.rotation = reader.number("anisotropyRotation", layer.rotation, -kMaxValue);
            layer.texture  = reader.texture("anisotropyTexture");
        }
    );
    read(
        "KHR_materials_transmission",
        [&](const ExtensionReader& reader)
        {
            auto& layer   = material.transmission.emplace();
            layer.factor  = reader.number("transmissionFactor", layer.factor, 0.0f, 1.0f);
            layer.texture = reader.texture("transmissionTexture");
        }
    );
    read(
        "KHR_materials_volume",
        [&](const ExtensionReader& reader)
        {
            auto& volume            = material.volume.emplace();
            volume.thicknessFactor  = reader.number("thicknessFactor", volume.thicknessFactor);
            volume.thicknessTexture = reader.texture("thicknessTexture");
            volume.attenuationColor = reader.color("attenuationColor", volume.attenuationColor);
            if (reader.value.Has("attenuationDistance"))
            {
                volume.attenuationDistance = reader.number("attenuationDistance", 0.0f);
                CHECK(*volume.attenuationDistance > 0.0f);
            }
        }
    );
    read(
        "KHR_materials_ior",
        [&](const ExtensionReader& reader)
        {
            material.ior = reader.number("ior", 1.5f);
            CHECK(*material.ior == 0.0f || *material.ior >= 1.0f);
        }
    );
    read(
        "KHR_materials_specular",
        [&](const ExtensionReader& reader)
        {
            auto& layer        = material.specular.emplace();
            layer.factor       = reader.number("specularFactor", layer.factor, 0.0f, 1.0f);
            layer.colorFactor  = reader.color("specularColorFactor", layer.colorFactor, kMaxValue);
            layer.texture      = reader.texture("specularTexture");
            layer.colorTexture = reader.texture("specularColorTexture", ColorSpace::Srgb);
        }
    );
    read(
        "KHR_materials_iridescence",
        [&](const ExtensionReader& reader)
        {
            auto& layer            = material.iridescence.emplace();
            layer.factor           = reader.number("iridescenceFactor", layer.factor, 0.0f, 1.0f);
            layer.ior              = reader.number("iridescenceIor", layer.ior, 1.0f);
            layer.thicknessMinimum = reader.number("iridescenceThicknessMinimum", layer.thicknessMinimum);
            layer.thicknessMaximum = reader.number("iridescenceThicknessMaximum", layer.thicknessMaximum);
            CHECK(layer.thicknessMaximum >= layer.thicknessMinimum);
            layer.texture          = reader.texture("iridescenceTexture");
            layer.thicknessTexture = reader.texture("iridescenceThicknessTexture");
        }
    );
    read(
        "KHR_materials_dispersion",
        [&](const ExtensionReader& reader) { material.dispersion = reader.number("dispersion", 0.0f); }
    );
    read(
        "KHR_materials_emissive_strength",
        [&](const ExtensionReader& reader) { material.emissiveStrength = reader.number("emissiveStrength", 1.0f); }
    );
    read("KHR_materials_unlit", [&](const ExtensionReader&) { material.unlit = true; });
    read(
        "KHR_materials_diffuse_transmission",
        [&](const ExtensionReader& reader)
        {
            auto& layer        = material.diffuseTransmission.emplace();
            layer.factor       = reader.number("diffuseTransmissionFactor", layer.factor, 0.0f, 1.0f);
            layer.colorFactor  = reader.color("diffuseTransmissionColorFactor", layer.colorFactor);
            layer.texture      = reader.texture("diffuseTransmissionTexture");
            layer.colorTexture = reader.texture("diffuseTransmissionColorTexture", ColorSpace::Srgb);
        }
    );
}
