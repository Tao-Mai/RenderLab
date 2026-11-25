#include "render/resources/MaterialTable.h"

#include "render/RhiUtils.h"

#include <slang-rhi.h>
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/GltfMaterial.h"
#include "core/Logger.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/SceneLightConstants.h"

#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

namespace
{
[[nodiscard]] NormalEncoding normalEncoding(const std::optional<TextureBinding>& binding, const AssetManager& assets)
{
    if (binding)
    {
        const ImageFormat format = assets.get<TextureAsset>(binding->textureID).format;
        if (format == ImageFormat::BC5Unorm)
        {
            return NormalEncoding::RgUnorm;
        }
        if (format == ImageFormat::BC5Snorm)
        {
            return NormalEncoding::RgSnorm;
        }
    }
    return NormalEncoding::Rgb;
}

}  // namespace


void MaterialTable::init(rhi::IDevice& targetDevice, RenderResourceManager& targetResources)
{
    device = &targetDevice;
    resources = &targetResources;
    static_cast<void>(textureReference({BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat}));
    samplers.fill(samplers[0]);

    rhiCheck(device->createBuffer(
        {.size = sizeof(MaterialData), .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
        nullptr,
        materialBuffer.writeRef()
    ));
    uploadedMaterialRevision.reset();
}

TextureRef MaterialTable::textureReference(const TextureBinding& binding)
{
    const auto [image, newImage] =
        textureIndices.try_emplace(binding.textureID, static_cast<uint32_t>(textureIndices.size()));
    if (newImage)
    {
        CHECK(
            image->second < kMaxMaterialTextures,
            "material texture '{}' exceeds capacity {}",
            binding.textureID,
            kMaxMaterialTextures
        );
        pendingImages.push_back(binding.textureID);
    }

    const auto [sampler, newSampler] =
        samplerIndices.try_emplace(binding.samplerID, static_cast<uint32_t>(samplerIndices.size()));
    if (newSampler)
    {
        CHECK(
            sampler->second < kMaxMaterialSamplers,
            "material sampler '{}' exceeds capacity {}",
            binding.samplerID,
            kMaxMaterialSamplers
        );
        samplers[sampler->second] = resources->sampler(binding.samplerID);
    }
    if (newSampler)
        textureBindings.clear();

    return {.textureIndex = image->second, .samplerIndex = sampler->second};
}

uint32_t MaterialTable::resolve(const AssetID<GltfMaterial>& id, const AssetManager& assets)
{
    const auto& material = assets.get(id);
    const bool sgWorkflow = material.workflow == MaterialWorkflow::SpecularGlossiness;
    CHECK(!sgWorkflow || material.specularGlossiness.has_value(), "SG parameters missing: {}", material.id);

    const auto alphaMode = material.alphaMode.value_or("OPAQUE");
    CHECK(
        alphaMode == "OPAQUE" || alphaMode == "MASK" || alphaMode == "BLEND",
        "material '{}' has an unknown alpha mode '{}'",
        material.id,
        alphaMode
    );
    const float emissiveStrength = material.emissiveStrength.value_or(1.0f);
    CHECK(std::isfinite(emissiveStrength) && emissiveStrength >= 0.0f);

    const auto  specular  = material.specular.value_or(GltfMaterial::Specular{});
    const float ior       = material.ior.value_or(1.5f);
    CHECK(std::isfinite(ior) && (ior == 0.0f || ior >= 1.0f), "invalid IOR in material '{}'", material.id);
    CHECK(
        std::isfinite(specular.factor) && specular.factor >= 0.0f && specular.factor <= 1.0f,
        "invalid specular factor in material '{}'",
        material.id
    );
    for (glm::length_t channel = 0; channel < 3; ++channel)
    {
        CHECK(
            std::isfinite(specular.colorFactor[channel]) && specular.colorFactor[channel] >= 0.0f,
            "invalid specular color in material '{}'",
            material.id
        );
    }
    const float iorRatio = ior == 0.0f ? 1.0f : (ior - 1.0f) / (ior + 1.0f);

    MaterialData data{
        .colorFactor         = material.baseColorFactor.value_or(glm::vec4{1.0f}),
        .emissiveFactor      = glm::vec4(material.emissive.value_or(glm::vec3{0.0f}) * emissiveStrength, 0.0f),
        .specularColorFactor = specular.colorFactor,
        .specularFactor      = specular.factor,
        .roughness           = material.roughness.value_or(1.0f),
        .metallic            = material.metallic.value_or(1.0f),
        .alphaCutoff         = material.alphaCutoff.value_or(0.5f),
        .alphaMode           = alphaMode == "MASK" ? kAlphaModeMask
                             : alphaMode == "BLEND" ? kAlphaModeBlend
                                                   : kAlphaModeOpaque,
        .normalScale         = material.normalScale.value_or(1.0f),
        .workflow            = static_cast<uint32_t>(material.workflow),
        .dielectricF0        = iorRatio * iorRatio,
    };

    const auto resolveTexture = [&](const std::optional<TextureBinding>& source, const TextureAsset::ID& fallback)
    {
        const auto binding = source.value_or(TextureBinding{fallback, BuiltinAssets::Sampler::linearRepeat});
        return textureReference(binding);
    };

    if (sgWorkflow)
    {
        const auto& sg = *material.specularGlossiness;
        CHECK(
            std::isfinite(sg.glossinessFactor) && sg.glossinessFactor >= 0.0f && sg.glossinessFactor <= 1.0f,
            "invalid glossiness in material '{}'",
            material.id
        );
        for (glm::length_t channel = 0; channel < 4; ++channel)
        {
            CHECK(
                std::isfinite(sg.diffuseFactor[channel]) && sg.diffuseFactor[channel] >= 0.0f &&
                    sg.diffuseFactor[channel] <= 1.0f,
                "invalid SG diffuse color in material '{}'",
                material.id
            );
        }
        for (glm::length_t channel = 0; channel < 3; ++channel)
        {
            CHECK(
                std::isfinite(sg.specularFactor[channel]) && sg.specularFactor[channel] >= 0.0f &&
                    sg.specularFactor[channel] <= 1.0f,
                "invalid SG specular color in material '{}'",
                material.id
            );
        }

        data.colorFactor          = sg.diffuseFactor;
        data.specularColorFactor  = sg.specularFactor;
        data.glossinessFactor     = sg.glossinessFactor;
        data.colorTexture         = resolveTexture(sg.diffuseTexture, BuiltinAssets::Texture::white);
        data.specularColorTexture = resolveTexture(sg.specularGlossinessTexture, BuiltinAssets::Texture::white);
    }
    else
    {
        data.colorTexture         = resolveTexture(material.baseColorTexture, BuiltinAssets::Texture::white);
        data.metallicTexture      = resolveTexture(material.metallicTexture, BuiltinAssets::Texture::kWhiteLinear);
        data.roughnessTexture     = resolveTexture(material.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear);
        data.specularColorTexture = resolveTexture(specular.colorTexture, BuiltinAssets::Texture::white);
        data.specularTexture      = resolveTexture(specular.texture, BuiltinAssets::Texture::kWhiteLinear);
    }

    data.normalTexture = resolveTexture(material.normalTexture, BuiltinAssets::Texture::flatNormal);
    data.normalEncoding  = normalEncoding(material.normalTexture, assets);
    data.emissiveTexture = resolveTexture(material.emissiveTexture, BuiltinAssets::Texture::white);
    if (material.doubleSided.value_or(true))
        data.emissiveFlags = kEmissiveTwoSided;

    return store(id, data, {
        .emissive    = glm::any(glm::greaterThan(glm::vec3{data.emissiveFactor}, glm::vec3{0.0f})),
    });
}

uint32_t MaterialTable::store(const AssetID<GltfMaterial>& id, const MaterialData& data, MaterialInfo info)
{
    const auto [found, added] = materialIndices.try_emplace(id, static_cast<uint32_t>(materials.size()));
    if (added)
    {
        materials.push_back(data);
        materialInfos.push_back(info);
        ++materialRevision;
        if (info.emissive)
            ++emissiveRevision;
    }
    else
    {
        auto& previous = materials[found->second];
        auto& previousInfo = materialInfos[found->second];
        if (previousInfo.emissive != info.emissive ||
            (info.emissive && (previous.emissiveFactor != data.emissiveFactor ||
                previous.emissiveTexture != data.emissiveTexture || previous.emissiveFlags != data.emissiveFlags)))
        {
            ++emissiveRevision;
        }
        if (previous != data)
        {
            previous = data;
            ++materialRevision;
        }
        previousInfo = info;
    }
    return found->second;
}

bool MaterialTable::upload(rhi::ICommandEncoder& commands)
{
    const bool imagesChanged = !pendingImages.empty();
    if (imagesChanged)
    {
        for (const auto& id : pendingImages)
        {
            const auto index = textureIndices.at(id);
            auto view = resources->texture(id, commands)->getDefaultView();
            if (index == 0)
                images.fill(view);
            else
                images[index] = std::move(view);
        }
        pendingImages.clear();
        textureBindings.clear();
    }

    const bool materialsChanged = uploadedMaterialRevision != materialRevision;
    if (materialsChanged)
    {
        const std::array<std::byte, sizeof(MaterialData)> empty{};
        const auto bytes = materials.empty() ? std::span<const std::byte>{empty} : std::as_bytes(std::span{materials});
        ensureBufferCapacity(*device, materialBuffer, bytes.size());
        rhiCheck(commands.uploadBufferData(materialBuffer, 0, bytes.size(), bytes.data()));
        uploadedMaterialRevision = materialRevision;
    }
    return materialsChanged || imagesChanged;
}

void MaterialTable::bind(rhi::IShaderObject& object) const
{
    const rhi::ShaderCursor root(&object);
    rhiCheck(root["materials"].setBinding(materialBuffer));
    const auto textures = root["materialTextures"];
    auto* layout = textures.getTypeLayout()->getElementTypeLayout();
    auto& binding = textureBindings[layout];
    if (!binding)
    {
        rhiCheck(device->createShaderObjectFromTypeLayout(object.getSlangComponentType(), layout, binding.writeRef()));
        const rhi::ShaderCursor table(binding);
        for (uint32_t index = 0; index < images.size(); ++index)
            rhiCheck(table["textures"][index].setBinding(images[index]));
        for (uint32_t index = 0; index < samplers.size(); ++index)
            rhiCheck(table["samplers"][index].setBinding(samplers[index]));
        rhiCheck(binding->finalize());
    }
    rhiCheck(textures.setObject(binding));
}
