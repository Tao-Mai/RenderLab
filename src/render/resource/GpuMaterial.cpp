#include "render/resource/GpuMaterial.h"
#include "render/feature/SceneInterface.h"

#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/device/Device.h"
#include "render/resource/ShaderData.h"

#include <cmath>
#include <utility>
#include <vector>

void GpuMaterial::create(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device&         device,
    Device&                         renderDevice,
    ShaderHandle                    targetShader,
    const PbrMaterialAsset&         material,
    Textures                        materialTextures
)
{
    shaderHandle = targetShader;
    textures     = std::move(materialTextures);
    const size_t uniformSize =
        material.clearcoat ? sizeof(MaterialUniforms) : offsetof(MaterialUniforms, clearcoatFactor);
    materialBuffer = std::make_shared<Buffer>(
        physicalDevice,
        device,
        uniformSize,
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    updateParameters(material);

    descriptorSet = renderDevice.allocateDescriptorSet(renderContext().sceneInterface->materialLayout);

    const vk::DescriptorBufferInfo materialBufferInfo{
        .buffer = materialBuffer->handle(),
        .offset = 0,
        .range  = uniformSize,
    };
    struct TextureDescriptor
    {
        uint32_t       imageBinding;
        uint32_t       samplerBinding;
        const Texture* texture;
    };
    std::vector bindings = {
        TextureDescriptor{
            RenderInterface::kMaterialImageBinding,
            RenderInterface::kMaterialSamplerBinding,
            &textures.baseColor
        },
        TextureDescriptor{
            RenderInterface::kMaterialNormalImageBinding,
            RenderInterface::kMaterialNormalSamplerBinding,
            &textures.normal
        },
        TextureDescriptor{
            RenderInterface::kMaterialMetallicImageBinding,
            RenderInterface::kMaterialMetallicSamplerBinding,
            &textures.metallic
        },
        TextureDescriptor{
            RenderInterface::kMaterialRoughnessImageBinding,
            RenderInterface::kMaterialRoughnessSamplerBinding,
            &textures.roughness
        },
        TextureDescriptor{
            RenderInterface::kMaterialEmissiveImageBinding,
            RenderInterface::kMaterialEmissiveSamplerBinding,
            &textures.emissive
        },
        TextureDescriptor{
            RenderInterface::kMaterialOcclusionImageBinding,
            RenderInterface::kMaterialOcclusionSamplerBinding,
            &textures.occlusion
        },
    };
    if (textures.clearcoat)
    {
        bindings.push_back(
            {RenderInterface::kMaterialClearcoatImageBinding,
             RenderInterface::kMaterialClearcoatSamplerBinding,
             &textures.clearcoat->factor}
        );
        bindings.push_back(
            {RenderInterface::kMaterialClearcoatRoughnessImageBinding,
             RenderInterface::kMaterialClearcoatRoughnessSamplerBinding,
             &textures.clearcoat->roughness}
        );
        bindings.push_back(
            {RenderInterface::kMaterialClearcoatNormalImageBinding,
             RenderInterface::kMaterialClearcoatNormalSamplerBinding,
             &textures.clearcoat->normal}
        );
    }
    bindings.push_back(
        {RenderInterface::kMaterialSheenColorImageBinding,
         RenderInterface::kMaterialSheenColorSamplerBinding,
         &textures.sheenColor}
    );
    bindings.push_back(
        {RenderInterface::kMaterialSheenRoughnessImageBinding,
         RenderInterface::kMaterialSheenRoughnessSamplerBinding,
         &textures.sheenRoughness}
    );
    std::vector<vk::DescriptorImageInfo> images(bindings.size());
    std::vector<vk::DescriptorImageInfo> samplers(bindings.size());
    std::vector<vk::WriteDescriptorSet>  writes(bindings.size() * 2 + 1);
    for (size_t index = 0; index < bindings.size(); ++index)
    {
        const auto& binding = bindings[index];
        DCHECK(binding.texture->image);
        DCHECK(binding.texture->sampler);
        images[index] = vk::DescriptorImageInfo{
            .imageView   = binding.texture->image->imageView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        samplers[index]   = vk::DescriptorImageInfo{.sampler = binding.texture->sampler};
        writes[index * 2] = vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = binding.imageBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .pImageInfo      = &images[index],
        };
        writes[index * 2 + 1] = vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = binding.samplerBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eSampler,
            .pImageInfo      = &samplers[index],
        };
    }
    writes.back() = vk::WriteDescriptorSet{
        .dstSet          = *descriptorSet,
        .dstBinding      = RenderInterface::kMaterialUniformBinding,
        .descriptorCount = 1,
        .descriptorType  = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo     = &materialBufferInfo,
    };
    device.updateDescriptorSets(writes, {});
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

    DCHECK(material.clearcoat.has_value() == textures.clearcoat.has_value());
    const float emissiveStrength = material.emissiveStrength.value_or(1.0f);
    CHECK(std::isfinite(emissiveStrength) && emissiveStrength >= 0.0f);

    const auto emissive  = material.emissive.value_or(glm::vec3{0.0f}) * emissiveStrength;
    const auto clearcoat = material.clearcoat.value_or(PbrMaterialAsset::Clearcoat{});
    const auto sheen     = material.sheen.value_or(PbrMaterialAsset::Sheen{});
    parameterData        = MaterialUniforms{
               .baseColorFactor          = material.baseColorFactor.value_or(glm::vec4{1.0f}),
               .roughness                = material.roughness.value_or(1.0f),
               .metallic                 = material.metallic.value_or(1.0f),
               .alphaCutoff              = material.alphaCutoff.value_or(0.5f),
               .alphaMode                = mode == MaterialAlphaMode::Mask ? 1u
                                  : mode == MaterialAlphaMode::Blend       ? 2u
                                                                           : 0u,
               .normalScale              = material.normalScale.value_or(1.0f),
               .occlusionStrength        = material.ao.value_or(1.0f),
               .emissiveFactor           = glm::vec4(emissive, 0.0f),
               .sheenColorFactor         = sheen.colorFactor,
               .sheenRoughnessFactor     = sheen.roughnessFactor,
               .clearcoatFactor          = clearcoat.factor,
               .clearcoatRoughnessFactor = clearcoat.roughnessFactor,
               .clearcoatNormalScale     = clearcoat.normalScale,
    };
    materialBuffer->upload(&parameterData, materialBuffer->size());
}

bool GpuMaterial::doubleSided() const { return pipelineState.cullMode == vk::CullModeFlagBits::eNone; }
