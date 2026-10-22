#include "render/resource/GpuMaterial.h"

#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/Logger.h"
#include "render/ShaderManager.h"
#include "render/device/Device.h"

#include <cmath>
#include <utility>
#include <vector>

void GpuMaterial::create(const PbrMaterialAsset& asset, ShaderManager& shaders, AssetManager& assets)
{
    const auto loadTexture = [&](const std::optional<TextureBinding>& source,
                                 const TextureAsset::ID&              fallback,
                                 std::optional<ColorSpace>            colorSpace = std::nullopt)
    {
        const TextureBinding binding = source.value_or(TextureBinding{fallback, BuiltinAssets::Sampler::linearRepeat});
        CHECK(
            !binding.textureID.empty() && !binding.samplerID.empty(),
            "material '{}' has an invalid texture binding",
            asset.id
        );
        if (colorSpace)
        {
            CHECK(
                assets.get<TextureAsset>(binding.textureID).colorSpace == *colorSpace,
                "material '{}' texture '{}' has an invalid color space",
                asset.id,
                binding.textureID
            );
        }
        return binding;
    };
    const auto sheen     = asset.sheen.value_or(PbrMaterialAsset::Sheen{});
    const auto clearcoat = asset.clearcoat.value_or(PbrMaterialAsset::Clearcoat{});
    textures             = Textures{
                    .baseColor      = loadTexture(asset.baseColorTexture, BuiltinAssets::Texture::white),
                    .normal         = loadTexture(asset.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear),
                    .metallic       = loadTexture(asset.metallicTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
                    .roughness      = loadTexture(asset.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
                    .emissive       = loadTexture(asset.emissiveTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb),
                    .occlusion      = loadTexture(asset.aoTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
                    .sheenColor     = loadTexture(sheen.colorTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb),
                    .sheenRoughness = loadTexture(sheen.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
                    .clearcoat      = {
                             .factor = loadTexture(clearcoat.texture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
                             .roughness =
                loadTexture(clearcoat.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
                             .normal = loadTexture(clearcoat.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear),
        },
    };

    shaderHandle = shaders.getOrLoad(
        asset.shaderId.value_or("scene"),
        {{"ENABLE_IBL", 1}, {"ENABLE_CLEARCOAT", asset.clearcoat ? 1 : 0}, {"ENABLE_SHEEN", asset.sheen ? 1 : 0}}
    );
    updateParameters(asset);
}

GraphicsPipelineInfo GpuMaterial::pipelineInfo(
    std::span<const vk::Format> colorFormats, vk::Format depthFormat, vk::PipelineLayout layout
) const
{
    return {
        .shader       = shaderHandle,
        .layout       = layout,
        .state        = pipelineState,
        .colorFormats = std::vector<vk::Format>(colorFormats.begin(), colorFormats.end()),
        .depthFormat  = depthFormat,
    };
}


void GpuMaterial::updateParameters(const PbrMaterialAsset& material)
{
    const std::string alphaMode = material.alphaMode.value_or("OPAQUE");
    if (alphaMode == "OPAQUE")
    {
        mode = MaterialAlphaMode::Opaque;
    }
    else if (alphaMode == "MASK")
    {
        mode = MaterialAlphaMode::Mask;
    }
    else
    {
        CHECK(alphaMode == "BLEND", "unknown material alpha mode '{}'", alphaMode);
        mode = MaterialAlphaMode::Blend;
    }
    pipelineState = {.depthWrite = mode != MaterialAlphaMode::Blend, .blend = mode == MaterialAlphaMode::Blend};
    pipelineState.cullMode =
        material.doubleSided.value_or(true) ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;

    const float emissiveStrength = material.emissiveStrength.value_or(1.0f);
    CHECK(std::isfinite(emissiveStrength) && emissiveStrength >= 0.0f);

    const auto emissive  = material.emissive.value_or(glm::vec3{0.0f}) * emissiveStrength;
    const auto clearcoat = material.clearcoat.value_or(PbrMaterialAsset::Clearcoat{});
    const auto sheen     = material.sheen.value_or(PbrMaterialAsset::Sheen{});
    parameterData        = MaterialData{
               .baseColorFactor          = material.baseColorFactor.value_or(glm::vec4{1.0f}),
               .roughness                = material.roughness.value_or(1.0f),
               .metallic                 = material.metallic.value_or(1.0f),
               .alphaCutoff              = material.alphaCutoff.value_or(0.5f),
               .alphaMode                = mode == MaterialAlphaMode::Mask ? 1u
                                  : mode == MaterialAlphaMode::Blend       ? 2u
                                                                           : 0u,
               .normalScale              = material.normalScale.value_or(1.0f),
               .occlusionStrength        = material.ao.value_or(1.0f),
               .doubleSided              = material.doubleSided.value_or(true) ? 1u : 0u,
               .emissiveFactor           = glm::vec4(emissive, 0.0f),
               .sheenColorFactor         = sheen.colorFactor,
               .sheenRoughnessFactor     = sheen.roughnessFactor,
               .clearcoatFactor          = clearcoat.factor,
               .clearcoatRoughnessFactor = clearcoat.roughnessFactor,
               .clearcoatNormalScale     = clearcoat.normalScale,
    };
}
