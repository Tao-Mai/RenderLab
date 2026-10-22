#include "render/resource/MaterialTable.h"

#include "asset/BuiltinAssets.h"
#include "core/Logger.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

#include <algorithm>
#include <string_view>

namespace
{
template <class T, size_t Capacity>
uint32_t addDescriptor(
    std::vector<T>&                                handles,
    std::array<vk::DescriptorImageInfo, Capacity>& descriptors,
    T                                              handle,
    vk::DescriptorImageInfo                        descriptor,
    std::string_view                               kind
)
{
    const auto found = std::ranges::find(handles, handle);
    if (found != handles.end())
        return static_cast<uint32_t>(found - handles.begin());

    CHECK(handles.size() < Capacity, "material {} descriptors exceed capacity {}", kind, Capacity);
    const auto index = static_cast<uint32_t>(handles.size());
    handles.push_back(handle);
    descriptors[index] = descriptor;
    return index;
}
}  // namespace

void MaterialTable::begin(RenderResourceManager& resources)
{
    materials.clear();
    materialIndices.clear();
    textureViews.clear();
    samplerHandles.clear();

    const auto white   = resources.texture(BuiltinAssets::Texture::white);
    const auto sampler = resources.sampler(BuiltinAssets::Sampler::linearRepeat);
    images.fill({.imageView = white->imageView(), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal});
    samplers.fill({.sampler = sampler});
}

uint32_t MaterialTable::add(const GpuMaterial& material)
{
    const auto [found, added] = materialIndices.try_emplace(&material, static_cast<uint32_t>(materials.size()));
    if (!added)
        return found->second;

    const auto bindTexture = [&](const GpuMaterial::Texture& texture)
    {
        DCHECK(texture.image && texture.sampler);
        const auto view = texture.image->imageView();
        return TextureRef{
            .textureIndex = addDescriptor(
                textureViews,
                images,
                view,
                {.imageView = view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal},
                "texture"
            ),
            .samplerIndex =
                addDescriptor(samplerHandles, samplers, texture.sampler, {.sampler = texture.sampler}, "sampler"),
        };
    };

    auto        data               = material.parameters();
    const auto& textures           = material.textureBindings();
    data.baseColorTexture          = bindTexture(textures.baseColor);
    data.normalTexture             = bindTexture(textures.normal);
    data.metallicTexture           = bindTexture(textures.metallic);
    data.roughnessTexture          = bindTexture(textures.roughness);
    data.emissiveTexture           = bindTexture(textures.emissive);
    data.occlusionTexture          = bindTexture(textures.occlusion);
    data.sheenColorTexture         = bindTexture(textures.sheenColor);
    data.sheenRoughnessTexture     = bindTexture(textures.sheenRoughness);
    data.clearcoatTexture          = bindTexture(textures.clearcoat.factor);
    data.clearcoatRoughnessTexture = bindTexture(textures.clearcoat.roughness);
    data.clearcoatNormalTexture    = bindTexture(textures.clearcoat.normal);
    materials.push_back(data);
    return found->second;
}

void MaterialTable::upload(const VulkanContext& vulkan, Device& device)
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
    const vk::DeviceSize bytes = std::max<size_t>(materials.size(), 1) * sizeof(MaterialData);
    CHECK(bytes <= limits.maxStorageBufferRange, "material data exceeds maxStorageBufferRange");
    const bool bufferChanged = bytes > materialBuffer.size();
    if (bufferChanged)
        materialBuffer = Buffer(
            vulkan.physicalDeviceHandle(),
            vulkan.deviceHandle(),
            bytes,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

    if (materials.empty())
    {
        const MaterialData empty{};
        materialBuffer.upload(&empty, sizeof(empty));
    }
    else
        materialBuffer.upload(materials.data(), materials.size() * sizeof(MaterialData));

    const bool newSet = !*descriptorSet;
    if (newSet)
        descriptorSet = device.allocateDescriptorSet(device.materialDescriptorSetLayout);

    const vk::DescriptorBufferInfo        bufferInfo{.buffer = materialBuffer.handle(), .range = materialBuffer.size()};
    std::array<vk::WriteDescriptorSet, 3> writes;
    uint32_t                              writeCount = 0;
    if (newSet || bufferChanged)
        writes[writeCount++] = {
            .dstSet          = *descriptorSet,
            .dstBinding      = RenderInterface::kMaterialBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &bufferInfo,
        };
    if (newSet || images != boundImages)
    {
        writes[writeCount++] = {
            .dstSet          = *descriptorSet,
            .dstBinding      = RenderInterface::kMaterialTexturesBinding,
            .descriptorCount = kMaxMaterialTextures,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .pImageInfo      = images.data(),
        };
        boundImages = images;
    }
    if (newSet || samplers != boundSamplers)
    {
        writes[writeCount++] = {
            .dstSet          = *descriptorSet,
            .dstBinding      = RenderInterface::kMaterialSamplersBinding,
            .descriptorCount = kMaxMaterialSamplers,
            .descriptorType  = vk::DescriptorType::eSampler,
            .pImageInfo      = samplers.data(),
        };
        boundSamplers = samplers;
    }
    if (writeCount)
        vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
}
