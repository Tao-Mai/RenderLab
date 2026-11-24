#include "render/resources/MaterialTable.h"

#include "render/device/RhiCheck.h"

#include <slang-rhi.h>
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/GltfMaterial.h"
#include "core/Logger.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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


void MaterialTable::init(RenderResourceManager& resources)
{
    static_cast<void>(
        textureReference({BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat}, resources)
    );
    images.fill(images[0]);
    samplers.fill(samplers[0]);
}

void MaterialTable::setup(GpuResources& resources, rhi::IDevice& device)
{
    rhi::BufferDesc desc;
    desc.size  = sizeof(MaterialData);
    desc.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
    rhi::ComPtr<rhi::IBuffer> buffer;
    rhiCheck(device.createBuffer(desc, nullptr, buffer.writeRef()));
    cache          = &resources;
    materialBuffer = cache->registerBuffer(buffer);
}
TextureRef MaterialTable::textureReference(const TextureBinding& binding, RenderResourceManager& resources)
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
        images[image->second] = resources.texture(binding.textureID)->getDefaultView();
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
        samplers[sampler->second] = resources.sampler(binding.samplerID);
    }
    return {.textureIndex = image->second, .samplerIndex = sampler->second};
}

size_t MaterialTable::MaterialKeyHash::operator()(const MaterialKey& key) const noexcept
{
    size_t     hash    = std::hash<AssetID<MaterialAsset>>{}(key.assetId);
    const auto combine = [&hash](uint32_t value)
    {
        constexpr size_t kHashCombineSeed = 0x9e3779b9;
        hash ^= std::hash<uint32_t>{}(value) + kHashCombineSeed + (hash << 6) + (hash >> 2);
    };
    combine(key.overrideOwner.has_value());
    if (key.overrideOwner)
    {
        combine(key.overrideOwner->objectId);
        combine(key.overrideOwner->instanceIndex);
        combine(key.overrideOwner->submeshIndex);
    }
    return hash;
}


uint32_t MaterialTable::update(
    const MaterialKey& key, const GltfMaterial& material, RenderResourceManager& resources, AssetManager& assets
)
{
    CHECK(material.workflow.has_value(), "material '{}' requires a workflow", material.id);
    CHECK(
        *material.workflow == MaterialWorkflow::MetallicRoughness ||
            *material.workflow == MaterialWorkflow::SpecularGlossiness,
        "invalid material workflow: {}",
        material.id
    );
    const bool sgWorkflow = *material.workflow == MaterialWorkflow::SpecularGlossiness;
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

    const auto  clearcoat = material.clearcoat.value_or(GltfMaterial::Clearcoat{});
    const auto  sheen     = material.sheen.value_or(GltfMaterial::Sheen{});
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
        .colorFactor              = material.baseColorFactor.value_or(glm::vec4{1.0f}),
        .roughness                = material.roughness.value_or(1.0f),
        .metallic                 = material.metallic.value_or(1.0f),
        .alphaCutoff              = material.alphaCutoff.value_or(0.5f),
        .alphaMode                = alphaMode == "MASK" ? kAlphaModeMask
                           : alphaMode == "BLEND"       ? kAlphaModeBlend
                                                        : kAlphaModeOpaque,
        .normalScale              = material.normalScale.value_or(1.0f),
        .occlusionStrength        = material.ao.value_or(1.0f),
        .doubleSided              = material.doubleSided.value_or(true) ? 1u : 0u,
        .workflow                 = static_cast<uint32_t>(*material.workflow),
        .emissiveFactor           = glm::vec4(material.emissive.value_or(glm::vec3{0.0f}) * emissiveStrength, 0.0f),
        .sheenColorFactor         = sheen.colorFactor,
        .sheenRoughnessFactor     = sheen.roughnessFactor,
        .clearcoatFactor          = clearcoat.factor,
        .clearcoatRoughnessFactor = clearcoat.roughnessFactor,
        .clearcoatNormalScale     = clearcoat.normalScale,
        .specularColorFactor      = specular.colorFactor,
        .specularFactor           = specular.factor,
        .dielectricF0             = iorRatio * iorRatio,
    };

    const auto resolveTexture = [&](const std::optional<TextureBinding>& source,
                                    const TextureAsset::ID&              fallback,
                                    std::optional<ColorSpace>            colorSpace = std::nullopt)
    {
        const auto binding = source.value_or(TextureBinding{fallback, BuiltinAssets::Sampler::linearRepeat});
        return textureReference(binding, resources);
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

    data.normalTexture = resolveTexture(material.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear);
    data.normalEncoding  = normalEncoding(material.normalTexture, assets);
    data.emissiveTexture = resolveTexture(material.emissiveTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb);
    data.occlusionTexture =
        resolveTexture(material.aoTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.sheenColorTexture = resolveTexture(sheen.colorTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb);
    data.sheenRoughnessTexture =
        resolveTexture(sheen.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatTexture = resolveTexture(clearcoat.texture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatRoughnessTexture =
        resolveTexture(clearcoat.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatNormalTexture =
        resolveTexture(clearcoat.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear);
    data.clearcoatNormalEncoding = normalEncoding(clearcoat.normalTexture, assets);

    const auto [found, added] = materialIndices.try_emplace(key, static_cast<uint32_t>(materials.size()));
    if (added)
    {
        materials.emplace_back();
        materialInfos.emplace_back();
    }
    if (added || materials[found->second] != data)
    {
        ++materialRevision;
        materials[found->second] = data;
    }
    materialInfos[found->second] = {
        .alphaMode   = data.alphaMode,
        .doubleSided = data.doubleSided != 0,
        .clearcoat   = material.clearcoat.has_value(),
        .sheen       = material.sheen.has_value(),
        .emissive    = glm::any(glm::greaterThan(glm::vec3{data.emissiveFactor}, glm::vec3{0.0f})),
    };
    return found->second;
}

void MaterialTable::upload()
{
    const auto bytes  = std::max<size_t>(materials.size(), 1) * sizeof(MaterialData);
    const auto upload = cache->allocateUpload(materialBuffer, bytes);
    if (materials.empty())
        std::memset(upload.data.data(), 0, bytes);
    else
        std::memcpy(upload.data.data(), materials.data(), materials.size() * sizeof(MaterialData));
}

void MaterialTable::bind(rhi::IShaderObject* object) const
{
    const rhi::ShaderCursor root(object);
    if (const auto field = root["materials"]; field.isValid())
        rhiCheck(field.setBinding(cache->getBuffer(materialBuffer)));
    if (const auto field = root["textures"]; field.isValid())
        for (uint32_t index = 0; index < images.size(); ++index)
            rhiCheck(field[index].setBinding(images[index]));
    if (const auto field = root["samplers"]; field.isValid())
        for (uint32_t index = 0; index < samplers.size(); ++index)
            rhiCheck(field[index].setBinding(samplers[index]));
}
