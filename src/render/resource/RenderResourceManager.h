#pragma once

#include "asset/MeshAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/TextureAsset.h"
#include "render/device/GpuUploadContext.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuTexture.h"

#include <memory>
#include <unordered_map>

#include <vulkan/vulkan_raii.hpp>

class AssetManager;
class AssetDataManager;
class VulkanContext;

class RenderResourceManager
{
public:
    ~RenderResourceManager();

    void init(VulkanContext& vulkan, AssetManager& assets, AssetDataManager& data);
    void shutdown() noexcept;

    GpuMesh&                    mesh(const MeshAsset::ID& id);
    std::shared_ptr<GpuTexture> texture(const TextureAsset::ID& id);
    [[nodiscard]] vk::Sampler   sampler(const SamplerAsset::ID& id);

private:
    AssetManager*                                                     assets     = nullptr;
    AssetDataManager*                                                 data       = nullptr;
    VulkanContext*                                                    vulkan     = nullptr;
    vk::raii::CommandPool                                             uploadPool = nullptr;
    std::unordered_map<MeshAsset::ID, std::unique_ptr<GpuMesh>>       meshes;
    std::unordered_map<TextureAsset::ID, std::shared_ptr<GpuTexture>> textures;
    std::unordered_map<SamplerAsset::ID, vk::raii::Sampler>           samplers;

    [[nodiscard]] GpuUploadContext uploadContext() const;
};
