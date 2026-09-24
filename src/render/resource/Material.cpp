#include "render/resource/Material.h"

#include "render/DescriptorManager.h"
#include "render/resource/ShaderData.h"
#include "core/Logger.h"

#include <array>
#include <utility>

void Material::create(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device&         device,
    DescriptorManager&              descriptors,
    ShaderHandle                     targetShader,
    const MaterialDesc&             material,
    std::shared_ptr<Texture>        texture)
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

    albedoTexture  = std::move(texture);
    materialBuffer = std::make_shared<Buffer>(
        physicalDevice,
        device,
        sizeof(MaterialUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
    const MaterialUniforms materialData{
        .baseColorFactor = material.baseColorFactor.value_or(glm::vec4{1.0f}),
        .roughness = material.roughness.value_or(1.0f),
        .metallic = material.metallic.value_or(1.0f),
        .alphaCutoff = material.alphaCutoff.value_or(0.5f),
        .alphaMode = mode == RenderMode::AlphaTest ? 1u :
            mode == RenderMode::Transparent ? 2u : 0u,
    };
    materialBuffer->upload(&materialData, sizeof(materialData));

    descriptorSet = descriptors.allocate(DescriptorLayoutPreset::Material);

    const vk::DescriptorImageInfo imageInfo{
        .imageView = albedoTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo samplerInfo{
        .sampler = albedoTexture->sampler(),
    };
    const vk::DescriptorBufferInfo materialBufferInfo{
        .buffer = materialBuffer->handle(),
        .offset = 0,
        .range = sizeof(MaterialUniforms),
    };
    const std::array writes = {
        vk::WriteDescriptorSet{
            .dstSet = *descriptorSet,
            .dstBinding = RenderInterface::materialImageBinding,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &imageInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet = *descriptorSet,
            .dstBinding = RenderInterface::materialSamplerBinding,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet = *descriptorSet,
            .dstBinding = RenderInterface::materialUniformBinding,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &materialBufferInfo,
        },
    };
    device.updateDescriptorSets(writes, {});
}

vk::DescriptorSet Material::descriptorSetHandle() const
{
    return *descriptorSet;
}

PipelineKey Material::pipelineKey(vk::Format colorFormat, vk::Format depthFormat) const
{
    return {
        .shader = shaderHandle,
        .layout = PipelineLayoutPreset::SceneMaterial,
        .state = pipelineState,
        .colorFormat = colorFormat,
        .depthFormat = depthFormat,
    };
}

RenderMode Material::renderMode() const
{
    return mode;
}
