#include "render/resource/material.h"

#include "render/device/vk_check.h"

#include <array>
#include <cstddef>
#include <utility>

namespace
{
struct alignas(16) MaterialUniforms
{
    glm::vec4 baseColorFactor{1.0f};
    float     roughness = 1.0f;
    float     metallic  = 1.0f;
    float     padding0  = 0.0f;
    float     padding1  = 0.0f;
};

static_assert(offsetof(MaterialUniforms, baseColorFactor) == 0);
static_assert(offsetof(MaterialUniforms, roughness) == 16);
static_assert(offsetof(MaterialUniforms, metallic) == 20);
static_assert(sizeof(MaterialUniforms) == 32);
}

void Material::create(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device&         device,
    vk::DescriptorPool              descriptorPool,
    vk::DescriptorSetLayout         descriptorSetLayout,
    const MaterialDesc&             material,
    std::shared_ptr<Texture>        texture)
{
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
    };
    materialBuffer->upload(&materialData, sizeof(materialData));

    const vk::DescriptorSetAllocateInfo allocationInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout,
    };
    auto descriptorSets = vkCheck(
        device.allocateDescriptorSets(allocationInfo), "vkAllocateDescriptorSets");
    descriptorSet = std::make_shared<vk::raii::DescriptorSet>(
        std::move(descriptorSets.front()));

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
            .dstSet = **descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &imageInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet = **descriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet = **descriptorSet,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &materialBufferInfo,
        },
    };
    device.updateDescriptorSets(writes, {});
}

vk::DescriptorSet Material::descriptorSetHandle() const
{
    return **descriptorSet;
}
