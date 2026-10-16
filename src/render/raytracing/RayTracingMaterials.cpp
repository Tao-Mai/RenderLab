#include "render/raytracing/RayTracingMaterials.h"

#include "asset/BuiltinAssets.h"
#include "core/Logger.h"
#include "render/device/VulkanContext.h"
#include "render/resource/Buffer.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/RenderResourceManager.h"

#include <algorithm>
#include <string_view>

namespace
{
template<class T, size_t Capacity>
uint32_t addDescriptor(
    std::vector<T>& handles,
    std::array<vk::DescriptorImageInfo, Capacity>& descriptors,
    T handle,
    vk::DescriptorImageInfo descriptor,
    std::string_view kind
)
{
    const auto found = std::ranges::find(handles, handle);
    if (found != handles.end())
        return static_cast<uint32_t>(found - handles.begin());

    CHECK(handles.size() < Capacity, "ray tracing {} descriptors exceed capacity {}", kind, Capacity);
    const auto index = static_cast<uint32_t>(handles.size());
    handles.push_back(handle);
    descriptors[index] = descriptor;
    return index;
}
} // namespace

RayTracingMaterials::RayTracingMaterials(RenderResourceManager& resources)
{
    const auto white = resources.texture(BuiltinAssets::Texture::white);
    const auto sampler = resources.sampler(BuiltinAssets::Sampler::linearRepeat);
    images.fill({.imageView = white->imageView(), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal});
    samplers.fill({.sampler = sampler});
}

uint32_t RayTracingMaterials::add(const GpuMaterial& material)
{
    const auto [found, added] = materialIndices.try_emplace(&material, static_cast<uint32_t>(materials.size()));
    if (!added)
        return found->second;

    const auto bindTexture = [&](const GpuMaterial::Texture& texture)
    {
        DCHECK(texture.image && texture.sampler);
        const auto view = texture.image->imageView();
        return RayTracingTextureRef{
            .textureIndex = addDescriptor(
                textureViews, images, view,
                {.imageView = view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal}, "texture"
            ),
            .samplerIndex = addDescriptor(
                samplerHandles, samplers, texture.sampler, {.sampler = texture.sampler}, "sampler"
            ),
        };
    };

    const auto& parameters = material.parameters();
    const auto& textures = material.textureBindings();
    materials.push_back({
        .baseColorFactor   = parameters.baseColorFactor,
        .emissiveFactor    = parameters.emissiveFactor,
        .metallic          = parameters.metallic,
        .roughness         = parameters.roughness,
        .normalScale       = parameters.normalScale,
        .occlusionStrength = parameters.occlusionStrength,
        .alphaCutoff       = parameters.alphaCutoff,
        .alphaMode         = parameters.alphaMode,
        .doubleSided       = material.doubleSided() ? 1u : 0u,
        .baseColorTexture  = bindTexture(textures.baseColor),
        .normalTexture     = bindTexture(textures.normal),
        .metallicTexture   = bindTexture(textures.metallic),
        .roughnessTexture  = bindTexture(textures.roughness),
        .emissiveTexture   = bindTexture(textures.emissive),
        .occlusionTexture  = bindTexture(textures.occlusion),
    });
    return found->second;
}

void RayTracingMaterials::upload(Buffer& buffer, const VulkanContext& vulkan) const
{
    const vk::DeviceSize bytes = std::max<size_t>(materials.size(), 1) * sizeof(RayTracingMaterialData);
    CHECK(bytes <= vulkan.properties().limits.maxStorageBufferRange,
        "ray tracing material data exceeds maxStorageBufferRange");
    if (bytes > buffer.size())
        buffer = Buffer(
            vulkan.physicalDeviceHandle(), vulkan.deviceHandle(), bytes,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

    if (materials.empty())
    {
        const RayTracingMaterialData empty{};
        buffer.upload(&empty, sizeof(empty));
    }
    else
        buffer.upload(materials.data(), materials.size() * sizeof(RayTracingMaterialData));
}
