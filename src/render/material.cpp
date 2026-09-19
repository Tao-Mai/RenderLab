#include "render/material.h"

#include "render/texture.h"

#include <array>
#include <utility>

bool Material::hasAlbedoMap() const
{
    return !albedoMap.empty();
}

void Material::createDescriptorSet(
    const vk::raii::Device &device,
    vk::DescriptorPool descriptorPool,
    vk::DescriptorSetLayout descriptorSetLayout,
    std::shared_ptr<Texture> texture)
{
    albedoTexture = std::move(texture);
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
