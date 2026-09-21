#include "render/material.h"

#include "render/buffer.h"
#include "render/texture.h"

#include <array>
#include <cstddef>
#include <utility>

namespace
{
struct alignas(16) MaterialData
{
    glm::vec4 baseColorFactor{1.0f};
    float roughness = 1.0f;
    float metallic = 1.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

static_assert(offsetof(MaterialData, baseColorFactor) == 0);
static_assert(offsetof(MaterialData, roughness) == 16);
static_assert(offsetof(MaterialData, metallic) == 20);
static_assert(sizeof(MaterialData) == 32);
}

bool Material::hasAlbedoMap() const
{
    return !albedoMap.empty();
}

void Material::createDescriptorSet(
    const vk::raii::PhysicalDevice &physicalDevice,
    const vk::raii::Device &device,
    vk::DescriptorPool descriptorPool,
    vk::DescriptorSetLayout descriptorSetLayout,
    std::shared_ptr<Texture> texture)
{
    albedoTexture = std::move(texture);
    materialBuffer = std::make_shared<Buffer>(
        physicalDevice,
        device,
        sizeof(MaterialData),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
    const MaterialData materialData{
        .baseColorFactor = baseColorFactor,
        .roughness = roughness,
        .metallic = metallic,
    };
    materialBuffer->upload(&materialData, sizeof(materialData));

    const vk::DescriptorSetAllocateInfo allocationInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout,
    };
    vk::raii::DescriptorSets descriptorSets(device, allocationInfo);
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
        .range = sizeof(MaterialData),
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

void Material::bind(
    vk::raii::CommandBuffer &commandBuffer,
    vk::PipelineLayout pipelineLayout) const
{
    const std::array descriptorSets = {**descriptorSet};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        0,
        descriptorSets,
        {});
}
