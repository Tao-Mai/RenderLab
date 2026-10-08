#include "render/resource/GpuMaterial.h"

#include "render/DescriptorManager.h"
#include "render/resource/ShaderData.h"
#include "core/Logger.h"

#include <array>
#include <utility>

void GpuMaterial::create(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device&         device,
    DescriptorManager&            descriptors,
    ShaderHandle                  targetShader,
    const MaterialAsset&          material,
    Textures                      materialTextures)
{
    shaderHandle = targetShader;
    const std::string alphaMode = material.alphaMode.value_or("OPAQUE");
    if (alphaMode == "OPAQUE")
    {
        mode = RenderMode::Opaque;
    }
    else if (alphaMode == "MASK")
    {
        mode = RenderMode::AlphaTest;
    }
    else
    {
        CHECK(alphaMode == "BLEND", "unknown material alpha mode '{}'", alphaMode);
        mode = RenderMode::Transparent;
    }
    pipelineState = PipelineState::preset(mode);
    if (material.doubleSided == false)
    {
        pipelineState.cullMode = vk::CullModeFlagBits::eBack;
    }

    textures = std::move(materialTextures);
    colorFactor = material.baseColorFactor.value_or(glm::vec4{1.0f});
    materialBuffer = std::make_shared<Buffer>(
        physicalDevice,
        device,
        sizeof(MaterialUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
    const MaterialUniforms materialData{
        .baseColorFactor = colorFactor,
        .roughness = material.roughness.value_or(1.0f),
        .metallic = material.metallic.value_or(1.0f),
        .alphaCutoff = material.alphaCutoff.value_or(0.5f),
        .alphaMode = mode == RenderMode::AlphaTest ? 1u :
            mode == RenderMode::Transparent ? 2u : 0u,
        .normalScale = material.normalScale.value_or(1.0f),
        .occlusionStrength = material.ao.value_or(1.0f),
        .emissiveFactor = glm::vec4(material.emissive.value_or(glm::vec3{0.0f}), 0.0f),
    };
    materialBuffer->upload(&materialData, sizeof(materialData));

    descriptorSet = descriptors.allocate(DescriptorLayoutPreset::Material);

    const vk::DescriptorBufferInfo materialBufferInfo{
        .buffer = materialBuffer->handle(),
        .offset = 0,
        .range = sizeof(MaterialUniforms),
    };
    struct TextureDescriptor
    {
        uint32_t imageBinding;
        uint32_t samplerBinding;
        const Texture* texture;
    };
    const std::array bindings = {
        TextureDescriptor{RenderInterface::kMaterialImageBinding,
                       RenderInterface::kMaterialSamplerBinding, &textures.baseColor},
        TextureDescriptor{RenderInterface::kMaterialNormalImageBinding,
                       RenderInterface::kMaterialNormalSamplerBinding, &textures.normal},
        TextureDescriptor{RenderInterface::kMaterialMetallicImageBinding,
                       RenderInterface::kMaterialMetallicSamplerBinding, &textures.metallic},
        TextureDescriptor{RenderInterface::kMaterialRoughnessImageBinding,
                       RenderInterface::kMaterialRoughnessSamplerBinding, &textures.roughness},
        TextureDescriptor{RenderInterface::kMaterialEmissiveImageBinding,
                       RenderInterface::kMaterialEmissiveSamplerBinding, &textures.emissive},
        TextureDescriptor{RenderInterface::kMaterialOcclusionImageBinding,
                       RenderInterface::kMaterialOcclusionSamplerBinding, &textures.occlusion},
    };
    std::array<vk::DescriptorImageInfo, bindings.size()> images;
    std::array<vk::DescriptorImageInfo, bindings.size()> samplers;
    std::array<vk::WriteDescriptorSet, bindings.size() * 2 + 1> writes;
    for (size_t index = 0; index < bindings.size(); ++index)
    {
        const auto& binding = bindings[index];
        DCHECK(binding.texture->image);
        DCHECK(binding.texture->sampler);
        images[index] = vk::DescriptorImageInfo{
            .imageView = binding.texture->image->imageView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        samplers[index] = vk::DescriptorImageInfo{.sampler = binding.texture->sampler};
        writes[index * 2] = vk::WriteDescriptorSet{
            .dstSet = *descriptorSet,
            .dstBinding = binding.imageBinding,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &images[index],
        };
        writes[index * 2 + 1] = vk::WriteDescriptorSet{
            .dstSet = *descriptorSet,
            .dstBinding = binding.samplerBinding,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplers[index],
        };
    }
    writes.back() = vk::WriteDescriptorSet{
        .dstSet = *descriptorSet,
        .dstBinding = RenderInterface::kMaterialUniformBinding,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &materialBufferInfo,
    };
    device.updateDescriptorSets(writes, {});
}

vk::DescriptorSet GpuMaterial::descriptorSetHandle() const
{
    return *descriptorSet;
}

PipelineKey GpuMaterial::pipelineKey(vk::Format colorFormat, vk::Format depthFormat) const
{
    return {
        .shader = shaderHandle,
        .layout = PipelineLayoutPreset::SceneMaterial,
        .state = pipelineState,
        .colorFormat = colorFormat,
        .depthFormat = depthFormat,
    };
}

RenderMode GpuMaterial::renderMode() const
{
    return mode;
}
