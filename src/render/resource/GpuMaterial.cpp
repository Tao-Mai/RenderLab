#include "render/resource/GpuMaterial.h"

#include "core/Logger.h"
#include "render/device/Device.h"

#include <cmath>
#include <utility>
#include <vector>

void GpuMaterial::create(ShaderHandle targetShader, const PbrMaterialAsset& material, Textures materialTextures)
{
    shaderHandle = targetShader;
    textures     = std::move(materialTextures);
    updateParameters(material);
}

GraphicsPipelineInfo GpuMaterial::pipelineInfo(
    std::span<const vk::Format> colorFormats, vk::Format depthFormat, const PipelineLayout& layout
) const
{
    return {
        .shader       = shaderHandle,
        .layout       = &layout,
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
