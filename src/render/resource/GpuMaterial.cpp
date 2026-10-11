#include "render/resource/GpuMaterial.h"
#include "render/feature/SceneInterface.h"

#include "core/Logger.h"
#include "render/DescriptorManager.h"
#include "render/resource/ShaderData.h"

#include <utility>
#include <vector>

void GpuMaterial::create(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device&         device,
    DescriptorManager&              descriptors,
    ShaderHandle                    targetShader,
    const MaterialAsset&            material,
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

    descriptorSet = descriptors.allocate(SceneInterface::materialLayout(descriptors));

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

PipelineKey GpuMaterial::pipelineKey(
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


void GpuMaterial::updateParameters(const MaterialAsset& material)
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

    colorFactor = material.baseColorFactor.value_or(glm::vec4{1.0f});
    DCHECK(material.clearcoat.has_value() == textures.clearcoat.has_value());
    const auto             clearcoat = material.clearcoat.value_or(MaterialAsset::Clearcoat{});
    const MaterialUniforms materialData{
        .baseColorFactor          = colorFactor,
        .roughness                = material.roughness.value_or(1.0f),
        .metallic                 = material.metallic.value_or(1.0f),
        .alphaCutoff              = material.alphaCutoff.value_or(0.5f),
        .alphaMode                = mode == MaterialAlphaMode::Mask ? 1u
                           : mode == MaterialAlphaMode::Blend       ? 2u
                                                                    : 0u,
        .normalScale              = material.normalScale.value_or(1.0f),
        .occlusionStrength        = material.ao.value_or(1.0f),
        .emissiveFactor           = glm::vec4(material.emissive.value_or(glm::vec3{0.0f}), 0.0f),
        .clearcoatFactor          = clearcoat.factor,
        .clearcoatRoughnessFactor = clearcoat.roughnessFactor,
        .clearcoatNormalScale     = clearcoat.normalScale,
    };
    materialBuffer->upload(&materialData, materialBuffer->size());
}
