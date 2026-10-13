#include "render/resource/RenderResourceManager.h"

#include "asset/ApplyOptionalFields.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/Logger.h"
#include "render/DescriptorManager.h"
#include "render/ShaderManager.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuTexture.h"
#include "scene/system/SceneRenderData.h"
#include "scene/system/Visibility.h"

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
}  // namespace

RenderResourceManager::~RenderResourceManager() { DCHECK(!vulkan); }

GpuUploadContext RenderResourceManager::uploadContext() const
{
    DCHECK(vulkan && *uploadPool);
    return {vulkan->physicalDeviceHandle(), vulkan->deviceHandle(), uploadPool, vulkan->queueHandle()};
}

void RenderResourceManager::init(
    VulkanContext&     targetVulkan,
    DescriptorManager& targetDescriptors,
    ShaderManager&     targetShaders,
    AssetManager&      targetAssets,
    AssetDataManager&  targetData
)
{
    assets      = &targetAssets;
    data        = &targetData;
    vulkan      = &targetVulkan;
    descriptors = &targetDescriptors;
    shaders     = &targetShaders;
    const vk::CommandPoolCreateInfo poolInfo{
        .flags            = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = vulkan->graphicsQueueFamilyIndex(),
    };
    uploadPool = vkCheck(vulkan->deviceHandle().createCommandPool(poolInfo));
}

void RenderResourceManager::shutdown() noexcept
{
    for (auto& frame : frameResources)
        frame = {};
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

    const MeshAsset&     meshAsset = assets->get<MeshAsset>(id);
    MeshGeometry         geometry  = data->readGeometry(meshAsset);
    std::vector<Submesh> parts;
    parts.reserve(meshAsset.submeshes.size());
    for (const MeshAsset::Submesh& submesh : meshAsset.submeshes)
    {
        parts.push_back({
            .firstIndex = submesh.firstIndex,
            .indexCount = submesh.indexCount,
            .material   = material(submesh.materialId),
        });
    }

    auto gpu =
        std::make_unique<GpuMesh>(uploadContext(), std::move(geometry), std::move(parts), vulkan->supportsRayTracing());
    return *meshes.emplace(id, std::move(gpu)).first->second;
}

std::shared_ptr<GpuMaterial> RenderResourceManager::material(const MaterialAsset::ID& id)
{
    auto&      entry    = materials[id];
    const auto revision = assets->revision<MaterialAsset>(id);
    if (!entry.gpu || entry.revision != revision)
    {
        entry.gpu      = createMaterial(assets->get<MaterialAsset>(id).as<PbrMaterialAsset>());
        entry.revision = revision;
    }
    return entry.gpu;
}

std::shared_ptr<GpuMaterial> RenderResourceManager::createMaterial(const PbrMaterialAsset& asset)
{
    DCHECK(descriptors);
    const auto loadTexture = [&](const std::optional<TextureBinding>& source,
                                 const TextureAsset::ID&              fallback,
                                 std::optional<ColorSpace>            colorSpace = std::nullopt)
    {
        const TextureBinding binding = source.value_or(TextureBinding{fallback, BuiltinAssets::Sampler::linearRepeat});
        CHECK(
            !binding.textureID.empty() && !binding.samplerID.empty(),
            "material '{}' has an invalid texture binding",
            asset.id
        );
        if (colorSpace)
        {
            CHECK(
                assets->get<TextureAsset>(binding.textureID).colorSpace == *colorSpace,
                "material '{}' texture '{}' has an invalid color space",
                asset.id,
                binding.textureID
            );
        }
        return GpuMaterial::Texture{texture(binding.textureID), sampler(binding.samplerID)};
    };
    GpuMaterial::Textures textures{
        .baseColor = loadTexture(asset.baseColorTexture, BuiltinAssets::Texture::white),
        .normal    = loadTexture(asset.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear),
        .metallic  = loadTexture(asset.metallicTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
        .roughness = loadTexture(asset.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
        .emissive  = loadTexture(asset.emissiveTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb),
        .occlusion = loadTexture(asset.aoTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
    };
    if (asset.clearcoat)
    {
        const auto& clearcoat = *asset.clearcoat;
        textures.clearcoat    = GpuMaterial::Textures::Clearcoat{
               .factor = loadTexture(clearcoat.texture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
               .roughness =
                loadTexture(clearcoat.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear),
               .normal = loadTexture(clearcoat.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear),
        };
    }

    auto gpu = std::make_shared<GpuMaterial>();
    gpu->create(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        *descriptors,
        shaders->getOrLoad(
            asset.shaderId.value_or("scene"),
            {{"ENABLE_IBL", 1}, {"ENABLE_CLEARCOAT", asset.clearcoat ? 1 : 0}}
        ),
        asset,
        std::move(textures)
    );
    return gpu;
}

std::shared_ptr<GpuTexture> RenderResourceManager::texture(const TextureAsset::ID& id)
{
    if (const auto found = textures.find(id); found != textures.end())
    {
        return found->second;
    }

    const TextureAsset&        asset = assets->get<TextureAsset>(id);
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

    DCHECK(assets && vulkan);
    const SamplerAsset& asset  = assets->get<SamplerAsset>(id);
    const auto&         limits = vulkan->properties().limits;

    CHECK(
        std::isfinite(asset.mipLodBias) && std::isfinite(asset.maxAnisotropy) && std::isfinite(asset.minLod) &&
            (!asset.maxLod || (std::isfinite(*asset.maxLod) && asset.minLod <= *asset.maxLod)) &&
            std::abs(asset.mipLodBias) <= limits.maxSamplerLodBias,
        "invalid sampler LOD settings: {}",
        id
    );
    if (asset.anisotropyEnable)
    {
        CHECK(
            vulkan->supportedFeatures().samplerAnisotropy && asset.maxAnisotropy >= 1.0f &&
                asset.maxAnisotropy <= limits.maxSamplerAnisotropy,
            "unsupported sampler anisotropy: {}",
            id
        );
    }
    if (asset.unnormalizedCoordinates)
    {
        using AddressMode = SamplerAsset::AddressMode;
        CHECK(
            asset.magFilter == asset.minFilter && asset.mipmapMode == SamplerAsset::MipmapMode::Nearest &&
                asset.minLod == 0.0f && asset.maxLod.has_value() && *asset.maxLod == 0.0f && !asset.anisotropyEnable &&
                !asset.compareEnable &&
                (asset.addressModeU == AddressMode::ClampToEdge || asset.addressModeU == AddressMode::ClampToBorder) &&
                (asset.addressModeV == AddressMode::ClampToEdge || asset.addressModeV == AddressMode::ClampToBorder),
            "invalid unnormalized sampler settings: {}",
            id
        );
    }

    const vk::SamplerCreateInfo info{
        .magFilter               = toVulkan(asset.magFilter),
        .minFilter               = toVulkan(asset.minFilter),
        .mipmapMode              = toVulkan(asset.mipmapMode),
        .addressModeU            = toVulkan(asset.addressModeU),
        .addressModeV            = toVulkan(asset.addressModeV),
        .addressModeW            = toVulkan(asset.addressModeW),
        .mipLodBias              = asset.mipLodBias,
        .anisotropyEnable        = asset.anisotropyEnable,
        .maxAnisotropy           = asset.maxAnisotropy,
        .compareEnable           = asset.compareEnable,
        .compareOp               = toVulkan(asset.compareOp),
        .minLod                  = asset.minLod,
        .maxLod                  = asset.maxLod.value_or(VK_LOD_CLAMP_NONE),
        .borderColor             = toVulkan(asset.borderColor),
        .unnormalizedCoordinates = asset.unnormalizedCoordinates,
    };
    auto gpu = vkCheck(vulkan->deviceHandle().createSampler(info));
    return *samplers.emplace(id, std::move(gpu)).first->second;
}

namespace
{
bool sameMaterialResources(const PbrMaterialAsset& a, const PbrMaterialAsset& b)
{
    if (a.shaderId != b.shaderId || a.clearcoat.has_value() != b.clearcoat.has_value())
        return false;
    if (a.clearcoat &&
        (a.clearcoat->texture != b.clearcoat->texture ||
         a.clearcoat->roughnessTexture != b.clearcoat->roughnessTexture ||
         a.clearcoat->normalTexture != b.clearcoat->normalTexture))
        return false;
    return a.baseColorTexture == b.baseColorTexture && a.normalTexture == b.normalTexture &&
        a.metallicTexture == b.metallicTexture && a.roughnessTexture == b.roughnessTexture &&
        a.emissiveTexture == b.emissiveTexture && a.aoTexture == b.aoTexture;
}
}  // namespace

const RenderData& RenderResourceManager::prepare(const SceneRenderData& scene, uint32_t frameIndex)
{
    DCHECK(frameIndex < frameResources.size());
    auto& frame = frameResources[frameIndex];
    auto& draws = frame.draws;
    draws.drawItems.clear();
    draws.opaqueDrawItems.clear();
    draws.transparentDrawItems.clear();
    draws.lights = scene.lighting.lights;
    for (auto& [key, instance] : frame.instances)
        instance.used = false;

    for (const auto& object : scene.objects)
    {
        auto&       gpuMesh   = mesh(object.mesh);
        const auto& meshAsset = assets->get<MeshAsset>(object.mesh);
        for (uint32_t index = 0; index < meshAsset.submeshes.size(); ++index)
        {
            const auto& submesh     = meshAsset.submeshes[index];
            auto        gpuMaterial = material(submesh.materialId);
            if (const auto override = object.materialOverrides.find(static_cast<int>(index));
                override != object.materialOverrides.end())
            {
                auto asset = assets->get<MaterialAsset>(submesh.materialId).as<PbrMaterialAsset>();
                applyOptionalFields(asset, override->second);
                auto& instance = frame.instances[{object.selectionId, object.componentIndex, index}];
                if (!instance.gpu || !sameMaterialResources(instance.asset, asset))
                    instance.gpu = createMaterial(asset);
                else if (instance.asset != asset)
                    instance.gpu->updateParameters(asset);
                instance.asset = std::move(asset);
                instance.used  = true;
                gpuMaterial    = instance.gpu;
            }
            const DrawItem draw{
                &gpuMesh,
                gpuMaterial,
                object.model,
                submesh.firstIndex,
                submesh.indexCount,
                object.selectionId,
                object.distanceSquared,
                object.visibilityMask
            };
            draws.drawItems.push_back(draw);
            if (object.visibilityMask & RenderVisibility::kMainView)
                (gpuMaterial->alphaMode() == MaterialAlphaMode::Blend ? draws.transparentDrawItems : draws.opaqueDrawItems)
                    .push_back(draw);
        }
    }
    std::erase_if(frame.instances, [](const auto& entry) { return !entry.second.used; });
    std::ranges::sort(draws.transparentDrawItems, std::greater{}, &DrawItem::distanceSquared);
    return draws;
}
