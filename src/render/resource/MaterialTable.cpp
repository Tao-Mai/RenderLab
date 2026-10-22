#include "render/resource/MaterialTable.h"

#include "asset/BuiltinAssets.h"
#include "core/Logger.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

#include <algorithm>

void MaterialTable::init(RenderResourceManager& resources, const VulkanContext& vulkan, Device& device)
{
    const auto& limits = vulkan.properties().limits;
    CHECK(
        limits.maxPerStageDescriptorSampledImages >= kMaxMaterialTextures &&
            limits.maxPerStageDescriptorSamplers >= kMaxMaterialSamplers,
        "global materials require {} texture and {} sampler descriptors; GPU limits are {} and {}",
        kMaxMaterialTextures,
        kMaxMaterialSamplers,
        limits.maxPerStageDescriptorSampledImages,
        limits.maxPerStageDescriptorSamplers
    );

    texture({BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat}, resources);
    images.fill(images[0]);
    samplers.fill(samplers[0]);

    for (auto& frame : frames)
    {
        frame.materialBuffer = Buffer(
            vulkan.physicalDeviceHandle(),
            vulkan.deviceHandle(),
            sizeof(MaterialData),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        frame.descriptorSet = device.allocateDescriptorSet(device.materialDescriptorSetLayout);

        const vk::DescriptorBufferInfo bufferInfo{
            .buffer = frame.materialBuffer.handle(),
            .range  = frame.materialBuffer.size()
        };
        const std::array writes{
            vk::WriteDescriptorSet{
                .dstSet          = *frame.descriptorSet,
                .dstBinding      = RenderInterface::kMaterialBufferBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .pBufferInfo     = &bufferInfo,
            },
            vk::WriteDescriptorSet{
                .dstSet          = *frame.descriptorSet,
                .dstBinding      = RenderInterface::kMaterialTexturesBinding,
                .descriptorCount = kMaxMaterialTextures,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = images.data(),
            },
            vk::WriteDescriptorSet{
                .dstSet          = *frame.descriptorSet,
                .dstBinding      = RenderInterface::kMaterialSamplersBinding,
                .descriptorCount = kMaxMaterialSamplers,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = samplers.data(),
            },
        };
        vulkan.deviceHandle().updateDescriptorSets(writes, {});
        frame.textureCount = static_cast<uint32_t>(textureIndices.size());
        frame.samplerCount = static_cast<uint32_t>(samplerIndices.size());
    }
}

TextureRef MaterialTable::texture(const TextureBinding& binding, RenderResourceManager& resources)
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
        images[image->second] = {
            .imageView   = resources.texture(binding.textureID)->imageView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
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
        samplers[sampler->second] = vk::DescriptorImageInfo{.sampler = resources.sampler(binding.samplerID)};
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

uint32_t MaterialTable::update(const MaterialKey& key, const GpuMaterial& material, RenderResourceManager& resources)
{
    const auto [found, added] = materialIndices.try_emplace(key, static_cast<uint32_t>(materials.size()));
    if (added)
        materials.emplace_back();

    auto        data               = material.parameters();
    const auto& textures           = material.textureBindings();
    data.baseColorTexture          = texture(textures.baseColor, resources);
    data.normalTexture             = texture(textures.normal, resources);
    data.metallicTexture           = texture(textures.metallic, resources);
    data.roughnessTexture          = texture(textures.roughness, resources);
    data.emissiveTexture           = texture(textures.emissive, resources);
    data.occlusionTexture          = texture(textures.occlusion, resources);
    data.sheenColorTexture         = texture(textures.sheenColor, resources);
    data.sheenRoughnessTexture     = texture(textures.sheenRoughness, resources);
    data.clearcoatTexture          = texture(textures.clearcoat.factor, resources);
    data.clearcoatRoughnessTexture = texture(textures.clearcoat.roughness, resources);
    data.clearcoatNormalTexture    = texture(textures.clearcoat.normal, resources);
    materials[found->second]       = data;
    return found->second;
}

void MaterialTable::upload(const VulkanContext& vulkan, uint32_t frameIndex)
{
    auto&                frame = frames[frameIndex];
    const vk::DeviceSize bytes = std::max<size_t>(materials.size(), 1) * sizeof(MaterialData);
    CHECK(bytes <= vulkan.properties().limits.maxStorageBufferRange, "material data exceeds maxStorageBufferRange");
    const bool bufferChanged = bytes > frame.materialBuffer.size();
    if (bufferChanged)
        frame.materialBuffer = Buffer(
            vulkan.physicalDeviceHandle(),
            vulkan.deviceHandle(),
            bytes,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

    if (materials.empty())
    {
        const MaterialData empty{};
        frame.materialBuffer.upload(&empty, sizeof(empty));
    }
    else
        frame.materialBuffer.upload(materials.data(), materials.size() * sizeof(MaterialData));

    const vk::DescriptorBufferInfo bufferInfo{
        .buffer = frame.materialBuffer.handle(),
        .range  = frame.materialBuffer.size()
    };
    std::array<vk::WriteDescriptorSet, 3> writes;
    uint32_t                              writeCount = 0;
    if (bufferChanged)
        writes[writeCount++] = {
            .dstSet          = *frame.descriptorSet,
            .dstBinding      = RenderInterface::kMaterialBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &bufferInfo,
        };

    const auto textureCount = static_cast<uint32_t>(textureIndices.size());
    if (frame.textureCount != textureCount)
    {
        writes[writeCount++] = {
            .dstSet          = *frame.descriptorSet,
            .dstBinding      = RenderInterface::kMaterialTexturesBinding,
            .dstArrayElement = frame.textureCount,
            .descriptorCount = textureCount - frame.textureCount,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .pImageInfo      = images.data() + frame.textureCount,
        };
        frame.textureCount = textureCount;
    }
    const auto samplerCount = static_cast<uint32_t>(samplerIndices.size());
    if (frame.samplerCount != samplerCount)
    {
        writes[writeCount++] = {
            .dstSet          = *frame.descriptorSet,
            .dstBinding      = RenderInterface::kMaterialSamplersBinding,
            .dstArrayElement = frame.samplerCount,
            .descriptorCount = samplerCount - frame.samplerCount,
            .descriptorType  = vk::DescriptorType::eSampler,
            .pImageInfo      = samplers.data() + frame.samplerCount,
        };
        frame.samplerCount = samplerCount;
    }
    if (writeCount)
        vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
}
