#include "render/resource/MaterialTable.h"

#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/PbrMaterialAsset.h"
#include "core/Logger.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
        frame.descriptorSet = device.allocateDescriptorSet(device.materialDescriptorSetLayout);

        const std::array writes{
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

void MaterialTable::setup(RenderGraph& targetGraph)
{
    graph          = &targetGraph;
    materialBuffer = graph->createBuffer({
        .size  = sizeof(MaterialData),
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    for (auto& frame : frames)
        frame.bufferRevision = 0;
}

void MaterialTable::use(RenderGraph& graph, vk::PipelineStageFlags2 stages) const
{
    graph.use(materialBuffer, {.stages = stages, .access = vk::AccessFlagBits2::eShaderStorageRead});
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

uint32_t MaterialTable::update(
    const MaterialKey& key, const PbrMaterialAsset& material, RenderResourceManager& resources, AssetManager& assets
)
{
    const auto alphaMode = material.alphaMode.value_or("OPAQUE");
    CHECK(
        alphaMode == "OPAQUE" || alphaMode == "MASK" || alphaMode == "BLEND",
        "material '{}' has an unknown alpha mode '{}'",
        material.id,
        alphaMode
    );
    const float emissiveStrength = material.emissiveStrength.value_or(1.0f);
    CHECK(std::isfinite(emissiveStrength) && emissiveStrength >= 0.0f);

    const auto   clearcoat = material.clearcoat.value_or(PbrMaterialAsset::Clearcoat{});
    const auto   sheen     = material.sheen.value_or(PbrMaterialAsset::Sheen{});
    MaterialData data{
        .baseColorFactor          = material.baseColorFactor.value_or(glm::vec4{1.0f}),
        .roughness                = material.roughness.value_or(1.0f),
        .metallic                 = material.metallic.value_or(1.0f),
        .alphaCutoff              = material.alphaCutoff.value_or(0.5f),
        .alphaMode                = alphaMode == "MASK" ? kAlphaModeMask
                           : alphaMode == "BLEND"       ? kAlphaModeBlend
                                                        : kAlphaModeOpaque,
        .normalScale              = material.normalScale.value_or(1.0f),
        .occlusionStrength        = material.ao.value_or(1.0f),
        .doubleSided              = material.doubleSided.value_or(true) ? 1u : 0u,
        .emissiveFactor           = glm::vec4(material.emissive.value_or(glm::vec3{0.0f}) * emissiveStrength, 0.0f),
        .sheenColorFactor         = sheen.colorFactor,
        .sheenRoughnessFactor     = sheen.roughnessFactor,
        .clearcoatFactor          = clearcoat.factor,
        .clearcoatRoughnessFactor = clearcoat.roughnessFactor,
        .clearcoatNormalScale     = clearcoat.normalScale,
    };

    const auto resolveTexture = [&](const std::optional<TextureBinding>& source,
                                    const TextureAsset::ID&              fallback,
                                    std::optional<ColorSpace>            colorSpace = std::nullopt)
    {
        const auto binding = source.value_or(TextureBinding{fallback, BuiltinAssets::Sampler::linearRepeat});
        return texture(binding, resources);
    };

    data.baseColorTexture = resolveTexture(material.baseColorTexture, BuiltinAssets::Texture::white);
    data.normalTexture = resolveTexture(material.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear);
    data.metallicTexture =
        resolveTexture(material.metallicTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.roughnessTexture =
        resolveTexture(material.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.emissiveTexture = resolveTexture(material.emissiveTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb);
    data.occlusionTexture =
        resolveTexture(material.aoTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.sheenColorTexture = resolveTexture(sheen.colorTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb);
    data.sheenRoughnessTexture =
        resolveTexture(sheen.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatTexture = resolveTexture(clearcoat.texture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatRoughnessTexture =
        resolveTexture(clearcoat.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatNormalTexture =
        resolveTexture(clearcoat.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear);

    const auto [found, added] = materialIndices.try_emplace(key, static_cast<uint32_t>(materials.size()));
    if (added)
    {
        materials.emplace_back();
        materialInfos.emplace_back();
    }
    materials[found->second]     = data;
    materialInfos[found->second] = {
        .shaderId    = material.shaderId.value_or("scene"),
        .alphaMode   = data.alphaMode,
        .doubleSided = data.doubleSided != 0,
        .clearcoat   = material.clearcoat.has_value(),
        .sheen       = material.sheen.has_value(),
    };
    return found->second;
}

// 更新material descriptor set
void MaterialTable::upload(const VulkanContext& vulkan, uint32_t frameIndex)
{
    auto&                frame  = frames[frameIndex];
    const vk::DeviceSize bytes  = std::max<size_t>(materials.size(), 1) * sizeof(MaterialData);
    const auto           upload = graph->allocateUpload(materialBuffer, bytes);
    if (materials.empty())
    {
        const MaterialData empty{};
        std::memcpy(upload.data.data(), &empty, sizeof(empty));
    }
    else
        std::memcpy(upload.data.data(), materials.data(), materials.size() * sizeof(MaterialData));

    const auto&                           buffer        = graph->getBuffer(materialBuffer, 0);
    const bool                            bufferChanged = frame.bufferRevision != graph->bufferRevision(materialBuffer);
    const vk::DescriptorBufferInfo        bufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
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
    frame.bufferRevision = graph->bufferRevision(materialBuffer);
}
