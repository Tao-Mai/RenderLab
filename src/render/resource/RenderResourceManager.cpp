#include "render/resource/RenderResourceManager.h"

#include "asset/AssetManager.h"
#include "asset/AssetDataManager.h"
#include "asset/Serializer.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/DescriptorManager.h"
#include "render/ShaderManager.h"
#include "render/device/VulkanContext.h"
#include "render/device/VkCheck.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuTexture.h"

#include <cmath>
#include <utility>
#include <vector>

namespace
{
[[nodiscard]] vk::Filter toVulkan(SamplerAsset::Filter filter)
{
    switch (filter)
    {
        case SamplerAsset::Filter::Nearest:
            return vk::Filter::eNearest;
        case SamplerAsset::Filter::Linear:
            return vk::Filter::eLinear;
    }
    LOG_FATAL("invalid sampler filter");
}

[[nodiscard]] vk::SamplerMipmapMode toVulkan(SamplerAsset::MipmapMode mode)
{
    switch (mode)
    {
        case SamplerAsset::MipmapMode::Nearest:
            return vk::SamplerMipmapMode::eNearest;
        case SamplerAsset::MipmapMode::Linear:
            return vk::SamplerMipmapMode::eLinear;
    }
    LOG_FATAL("invalid sampler mipmap mode");
}

[[nodiscard]] vk::SamplerAddressMode toVulkan(SamplerAsset::AddressMode mode)
{
    switch (mode)
    {
        case SamplerAsset::AddressMode::Repeat:
            return vk::SamplerAddressMode::eRepeat;
        case SamplerAsset::AddressMode::MirroredRepeat:
            return vk::SamplerAddressMode::eMirroredRepeat;
        case SamplerAsset::AddressMode::ClampToEdge:
            return vk::SamplerAddressMode::eClampToEdge;
        case SamplerAsset::AddressMode::ClampToBorder:
            return vk::SamplerAddressMode::eClampToBorder;
    }
    LOG_FATAL("invalid sampler address mode");
}

[[nodiscard]] vk::CompareOp toVulkan(SamplerAsset::CompareOp op)
{
    switch (op)
    {
        case SamplerAsset::CompareOp::Never:
            return vk::CompareOp::eNever;
        case SamplerAsset::CompareOp::Less:
            return vk::CompareOp::eLess;
        case SamplerAsset::CompareOp::Equal:
            return vk::CompareOp::eEqual;
        case SamplerAsset::CompareOp::LessOrEqual:
            return vk::CompareOp::eLessOrEqual;
        case SamplerAsset::CompareOp::Greater:
            return vk::CompareOp::eGreater;
        case SamplerAsset::CompareOp::NotEqual:
            return vk::CompareOp::eNotEqual;
        case SamplerAsset::CompareOp::GreaterOrEqual:
            return vk::CompareOp::eGreaterOrEqual;
        case SamplerAsset::CompareOp::Always:
            return vk::CompareOp::eAlways;
    }
    LOG_FATAL("invalid sampler compare operation");
}

[[nodiscard]] vk::BorderColor toVulkan(SamplerAsset::BorderColor color)
{
    switch (color)
    {
        case SamplerAsset::BorderColor::FloatTransparentBlack:
            return vk::BorderColor::eFloatTransparentBlack;
        case SamplerAsset::BorderColor::IntTransparentBlack:
            return vk::BorderColor::eIntTransparentBlack;
        case SamplerAsset::BorderColor::FloatOpaqueBlack:
            return vk::BorderColor::eFloatOpaqueBlack;
        case SamplerAsset::BorderColor::IntOpaqueBlack:
            return vk::BorderColor::eIntOpaqueBlack;
        case SamplerAsset::BorderColor::FloatOpaqueWhite:
            return vk::BorderColor::eFloatOpaqueWhite;
        case SamplerAsset::BorderColor::IntOpaqueWhite:
            return vk::BorderColor::eIntOpaqueWhite;
    }
    LOG_FATAL("invalid sampler border color");
}
}

RenderResourceManager::~RenderResourceManager() = default;

GpuUploadContext RenderResourceManager::uploadContext() const
{
    DCHECK(vulkan && *uploadPool,
          "RenderResourceManager upload context is not initialized");
    return {vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
            uploadPool, vulkan->queueHandle()};
}

void RenderResourceManager::init(VulkanContext&     targetVulkan,
                                 DescriptorManager& targetDescriptors, ShaderManager& targetShaders)
{
    DCHECK(context().assetManager);
    DCHECK(context().assetDataManager);
    assets      = context().assetManager;
    data        = context().assetDataManager;
    vulkan      = &targetVulkan;
    descriptors = &targetDescriptors;
    shaders     = &targetShaders;
    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = vulkan->graphicsQueueFamilyIndex(),
    };
    uploadPool = vkCheck(vulkan->deviceHandle().createCommandPool(poolInfo));
}

void RenderResourceManager::reset() noexcept
{
    meshes.clear();
    materials.clear();
    textures.clear();
    samplers.clear();
    uploadPool  = nullptr;
    descriptors = nullptr;
    shaders     = nullptr;
    vulkan      = nullptr;
    assets      = nullptr;
    data        = nullptr;
}

GpuMesh& RenderResourceManager::mesh(const MeshAsset::ID& id)
{
    if (const auto found = meshes.find(id); found != meshes.end())
    {
        return *found->second;
    }

    const MeshAsset&    meshAsset = assets->get<MeshAsset>(id);
    MeshGeometry         geometry = data->readGeometry(meshAsset);
    std::vector<Submesh> parts;
    parts.reserve(meshAsset.submeshes.size());
    for (const MeshAsset::Submesh& submesh : meshAsset.submeshes)
    {
        parts.push_back({
            .firstIndex = submesh.firstIndex,
            .indexCount = submesh.indexCount,
            .material = &material(submesh.materialId),
        });
    }

    auto gpu = std::make_unique<GpuMesh>(
        uploadContext(),
        std::move(geometry),
        std::move(parts));
    return *meshes.emplace(id, std::move(gpu)).first->second;
}

MaterialAsset RenderResourceManager::materialAsset(const MaterialAsset::ID& id) const
{
    DCHECK(assets);
    return assets->get<MaterialAsset>(id);
}

GpuMaterial& RenderResourceManager::material(const MaterialAsset::ID& id)
{
    return material(materialAsset(id));
}

GpuMaterial& RenderResourceManager::material(const MaterialAsset& asset)
{
    DCHECK(descriptors);

    const std::string key = Serialize(asset).dump();
    if (const auto found = materials.find(key); found != materials.end())
    {
        return *found->second;
    }

    CHECK(asset.baseColorTexture.has_value() &&
          !asset.baseColorTexture->textureID.empty() &&
          !asset.baseColorTexture->samplerID.empty(),
          "material '{}' missing baseColorTexture",
          asset.id);

    std::shared_ptr<GpuTexture> baseColor = texture(asset.baseColorTexture->textureID);
    const vk::Sampler baseColorSampler = sampler(asset.baseColorTexture->samplerID);
    auto                     gpu       = std::make_unique<GpuMaterial>();
    gpu->create(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        *descriptors,
        shaders->getOrLoad(asset.shaderId.value_or("scene")),
        asset,
        std::move(baseColor), baseColorSampler);
    return *materials.emplace(key, std::move(gpu)).first->second;
}

std::shared_ptr<GpuTexture> RenderResourceManager::texture(const TextureAsset::ID& id)
{
    if (const auto found = textures.find(id); found != textures.end())
    {
        return found->second;
    }

    const TextureAsset&       asset  = assets->get<TextureAsset>(id);
    const std::vector<uint8_t> bytes = data->readTexture(asset);
    auto                       gpu   = std::make_shared<GpuTexture>(uploadContext(), asset, bytes);

    return textures.emplace(id, std::move(gpu)).first->second;
}

vk::Sampler RenderResourceManager::sampler(const SamplerAsset::ID& id)
{
    if (const auto found = samplers.find(id); found != samplers.end())
    {
        return *found->second;
    }

    DCHECK(assets && vulkan,
          "RenderResourceManager is not initialized");
    const SamplerAsset& asset = assets->get<SamplerAsset>(id);
    const auto& limits = vulkan->properties().limits;

    CHECK(std::isfinite(asset.mipLodBias) &&
          std::isfinite(asset.maxAnisotropy) &&
          std::isfinite(asset.minLod) &&
          (!asset.maxLod ||
           (std::isfinite(*asset.maxLod) && asset.minLod <= *asset.maxLod)) &&
          std::abs(asset.mipLodBias) <= limits.maxSamplerLodBias,
          "invalid sampler LOD settings: {}", id);
    if (asset.anisotropyEnable)
    {
        CHECK(vulkan->supportedFeatures().samplerAnisotropy &&
              asset.maxAnisotropy >= 1.0f &&
              asset.maxAnisotropy <= limits.maxSamplerAnisotropy,
              "unsupported sampler anisotropy: {}", id);
    }
    if (asset.unnormalizedCoordinates)
    {
        using AddressMode = SamplerAsset::AddressMode;
        CHECK(asset.magFilter == asset.minFilter &&
              asset.mipmapMode == SamplerAsset::MipmapMode::Nearest &&
              asset.minLod == 0.0f &&
              asset.maxLod.has_value() && *asset.maxLod == 0.0f &&
              !asset.anisotropyEnable && !asset.compareEnable &&
              (asset.addressModeU == AddressMode::ClampToEdge ||
               asset.addressModeU == AddressMode::ClampToBorder) &&
              (asset.addressModeV == AddressMode::ClampToEdge ||
               asset.addressModeV == AddressMode::ClampToBorder),
              "invalid unnormalized sampler settings: {}", id);
    }

    const vk::SamplerCreateInfo info{
        .magFilter = toVulkan(asset.magFilter),
        .minFilter = toVulkan(asset.minFilter),
        .mipmapMode = toVulkan(asset.mipmapMode),
        .addressModeU = toVulkan(asset.addressModeU),
        .addressModeV = toVulkan(asset.addressModeV),
        .addressModeW = toVulkan(asset.addressModeW),
        .mipLodBias = asset.mipLodBias,
        .anisotropyEnable = asset.anisotropyEnable,
        .maxAnisotropy = asset.maxAnisotropy,
        .compareEnable = asset.compareEnable,
        .compareOp = toVulkan(asset.compareOp),
        .minLod = asset.minLod,
        .maxLod = asset.maxLod.value_or(VK_LOD_CLAMP_NONE),
        .borderColor = toVulkan(asset.borderColor),
        .unnormalizedCoordinates = asset.unnormalizedCoordinates,
    };
    auto gpu = vkCheck(vulkan->deviceHandle().createSampler(info));
    return *samplers.emplace(id, std::move(gpu)).first->second;
}
