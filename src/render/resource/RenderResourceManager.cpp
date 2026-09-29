#include "render/resource/RenderResourceManager.h"

#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
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

#include <rfl/json.hpp>

namespace
{
[[nodiscard]] vk::Filter toVulkan(Sampler::Desc::Filter filter)
{
    switch (filter)
    {
        case Sampler::Desc::Filter::Nearest:
            return vk::Filter::eNearest;
        case Sampler::Desc::Filter::Linear:
            return vk::Filter::eLinear;
    }
    LOG_FATAL("invalid sampler filter");
}

[[nodiscard]] vk::SamplerMipmapMode toVulkan(Sampler::Desc::MipmapMode mode)
{
    switch (mode)
    {
        case Sampler::Desc::MipmapMode::Nearest:
            return vk::SamplerMipmapMode::eNearest;
        case Sampler::Desc::MipmapMode::Linear:
            return vk::SamplerMipmapMode::eLinear;
    }
    LOG_FATAL("invalid sampler mipmap mode");
}

[[nodiscard]] vk::SamplerAddressMode toVulkan(Sampler::Desc::AddressMode mode)
{
    switch (mode)
    {
        case Sampler::Desc::AddressMode::Repeat:
            return vk::SamplerAddressMode::eRepeat;
        case Sampler::Desc::AddressMode::MirroredRepeat:
            return vk::SamplerAddressMode::eMirroredRepeat;
        case Sampler::Desc::AddressMode::ClampToEdge:
            return vk::SamplerAddressMode::eClampToEdge;
        case Sampler::Desc::AddressMode::ClampToBorder:
            return vk::SamplerAddressMode::eClampToBorder;
    }
    LOG_FATAL("invalid sampler address mode");
}

[[nodiscard]] vk::CompareOp toVulkan(Sampler::Desc::CompareOp op)
{
    switch (op)
    {
        case Sampler::Desc::CompareOp::Never:
            return vk::CompareOp::eNever;
        case Sampler::Desc::CompareOp::Less:
            return vk::CompareOp::eLess;
        case Sampler::Desc::CompareOp::Equal:
            return vk::CompareOp::eEqual;
        case Sampler::Desc::CompareOp::LessOrEqual:
            return vk::CompareOp::eLessOrEqual;
        case Sampler::Desc::CompareOp::Greater:
            return vk::CompareOp::eGreater;
        case Sampler::Desc::CompareOp::NotEqual:
            return vk::CompareOp::eNotEqual;
        case Sampler::Desc::CompareOp::GreaterOrEqual:
            return vk::CompareOp::eGreaterOrEqual;
        case Sampler::Desc::CompareOp::Always:
            return vk::CompareOp::eAlways;
    }
    LOG_FATAL("invalid sampler compare operation");
}

[[nodiscard]] vk::BorderColor toVulkan(Sampler::Desc::BorderColor color)
{
    switch (color)
    {
        case Sampler::Desc::BorderColor::FloatTransparentBlack:
            return vk::BorderColor::eFloatTransparentBlack;
        case Sampler::Desc::BorderColor::IntTransparentBlack:
            return vk::BorderColor::eIntTransparentBlack;
        case Sampler::Desc::BorderColor::FloatOpaqueBlack:
            return vk::BorderColor::eFloatOpaqueBlack;
        case Sampler::Desc::BorderColor::IntOpaqueBlack:
            return vk::BorderColor::eIntOpaqueBlack;
        case Sampler::Desc::BorderColor::FloatOpaqueWhite:
            return vk::BorderColor::eFloatOpaqueWhite;
        case Sampler::Desc::BorderColor::IntOpaqueWhite:
            return vk::BorderColor::eIntOpaqueWhite;
    }
    LOG_FATAL("invalid sampler border color");
}
}

RenderResourceManager::~RenderResourceManager() = default;

GpuUploadContext RenderResourceManager::uploadContext() const
{
    CHECK(vulkan != nullptr && *uploadPool,
          "RenderResourceManager upload context is not initialized");
    return {vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
            uploadPool, vulkan->queueHandle()};
}

void RenderResourceManager::init(VulkanContext&     targetVulkan,
                                 DescriptorManager& targetDescriptors, ShaderManager& targetShaders)
{
    CHECK(context().assetDescManager != nullptr,
          "AssetDescManager must exist before RenderResourceManager");
    CHECK(context().assetDataManager != nullptr,
          "AssetDataManager must exist before RenderResourceManager");
    assets      = context().assetDescManager;
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

GpuMesh& RenderResourceManager::mesh(const Mesh::ID& id)
{
    if (const auto found = meshes.find(id); found != meshes.end())
    {
        return *found->second;
    }

    const Mesh::Desc&    meshDesc = assets->desc<Mesh>(id);
    MeshGeometry         geometry = data->readGeometry(meshDesc);
    std::vector<Submesh> parts;
    parts.reserve(meshDesc.submeshes.size());
    for (const Mesh::Desc::Submesh& submesh : meshDesc.submeshes)
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

Material::Desc RenderResourceManager::materialDesc(const Material::ID& id) const
{
    CHECK(assets != nullptr, "RenderResourceManager is not initialized");
    return assets->desc<Material>(id);
}

GpuMaterial& RenderResourceManager::material(const Material::ID& id)
{
    return material(materialDesc(id));
}

GpuMaterial& RenderResourceManager::material(const Material::Desc& desc)
{
    CHECK(descriptors != nullptr, "RenderResourceManager is not initialized");

    const std::string key = rfl::json::write(desc);
    if (const auto found = materials.find(key); found != materials.end())
    {
        return *found->second;
    }

    CHECK(desc.baseColorTexture.has_value() &&
          !desc.baseColorTexture->textureID.empty() &&
          !desc.baseColorTexture->samplerID.empty(),
          "material '{}' missing baseColorTexture",
          desc.id);

    std::shared_ptr<GpuTexture> baseColor = texture(desc.baseColorTexture->textureID);
    const vk::Sampler baseColorSampler = sampler(desc.baseColorTexture->samplerID);
    auto                     gpu       = std::make_unique<GpuMaterial>();
    gpu->create(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        *descriptors,
        shaders->getOrLoad(desc.shaderId.value_or("scene")),
        desc,
        std::move(baseColor), baseColorSampler);
    return *materials.emplace(key, std::move(gpu)).first->second;
}

std::shared_ptr<GpuTexture> RenderResourceManager::texture(const Texture::ID& id)
{
    if (const auto found = textures.find(id); found != textures.end())
    {
        return found->second;
    }

    const Texture::Desc&       desc  = assets->desc<Texture>(id);
    const std::vector<uint8_t> bytes = data->readTexture(desc);
    auto                       gpu   = std::make_shared<GpuTexture>(uploadContext(), desc, bytes);

    return textures.emplace(id, std::move(gpu)).first->second;
}

vk::Sampler RenderResourceManager::sampler(const Sampler::ID& id)
{
    if (const auto found = samplers.find(id); found != samplers.end())
    {
        return *found->second;
    }

    CHECK(assets != nullptr && vulkan != nullptr,
          "RenderResourceManager is not initialized");
    const Sampler::Desc& desc = assets->desc<Sampler>(id);
    const auto& limits = vulkan->properties().limits;

    CHECK(std::isfinite(desc.mipLodBias) &&
          std::isfinite(desc.maxAnisotropy) &&
          std::isfinite(desc.minLod) &&
          (!desc.maxLod ||
           (std::isfinite(*desc.maxLod) && desc.minLod <= *desc.maxLod)) &&
          std::abs(desc.mipLodBias) <= limits.maxSamplerLodBias,
          "invalid sampler LOD settings: {}", id);
    if (desc.anisotropyEnable)
    {
        CHECK(vulkan->supportedFeatures().samplerAnisotropy &&
              desc.maxAnisotropy >= 1.0f &&
              desc.maxAnisotropy <= limits.maxSamplerAnisotropy,
              "unsupported sampler anisotropy: {}", id);
    }
    if (desc.unnormalizedCoordinates)
    {
        using AddressMode = Sampler::Desc::AddressMode;
        CHECK(desc.magFilter == desc.minFilter &&
              desc.mipmapMode == Sampler::Desc::MipmapMode::Nearest &&
              desc.minLod == 0.0f &&
              desc.maxLod.has_value() && *desc.maxLod == 0.0f &&
              !desc.anisotropyEnable && !desc.compareEnable &&
              (desc.addressModeU == AddressMode::ClampToEdge ||
               desc.addressModeU == AddressMode::ClampToBorder) &&
              (desc.addressModeV == AddressMode::ClampToEdge ||
               desc.addressModeV == AddressMode::ClampToBorder),
              "invalid unnormalized sampler settings: {}", id);
    }

    const vk::SamplerCreateInfo info{
        .magFilter = toVulkan(desc.magFilter),
        .minFilter = toVulkan(desc.minFilter),
        .mipmapMode = toVulkan(desc.mipmapMode),
        .addressModeU = toVulkan(desc.addressModeU),
        .addressModeV = toVulkan(desc.addressModeV),
        .addressModeW = toVulkan(desc.addressModeW),
        .mipLodBias = desc.mipLodBias,
        .anisotropyEnable = desc.anisotropyEnable,
        .maxAnisotropy = desc.maxAnisotropy,
        .compareEnable = desc.compareEnable,
        .compareOp = toVulkan(desc.compareOp),
        .minLod = desc.minLod,
        .maxLod = desc.maxLod.value_or(VK_LOD_CLAMP_NONE),
        .borderColor = toVulkan(desc.borderColor),
        .unnormalizedCoordinates = desc.unnormalizedCoordinates,
    };
    auto gpu = vkCheck(vulkan->deviceHandle().createSampler(info));
    return *samplers.emplace(id, std::move(gpu)).first->second;
}
