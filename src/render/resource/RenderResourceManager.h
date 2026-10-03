#pragma once

#include "asset/Asset.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuTexture.h"
#include "render/device/GpuUploadContext.h"

#include <memory>
#include <unordered_map>

#include <vulkan/vulkan_raii.hpp>

class AssetManager;
class AssetDataManager;
class DescriptorManager;
class ShaderManager;
class VulkanContext;

class RenderResourceManager
{
public:
    ~RenderResourceManager();

    void init(VulkanContext& vulkan, DescriptorManager& descriptors,
        ShaderManager& shaders);
    void reset() noexcept;

    GpuMesh& mesh(const MeshAsset::ID& id);
    [[nodiscard]] MaterialAsset materialAsset(const MaterialAsset::ID& id) const;
    GpuMaterial& material(const MaterialAsset::ID& id);
    GpuMaterial& material(const MaterialAsset& asset);
    std::shared_ptr<GpuTexture> texture(const TextureAsset::ID& id);
    [[nodiscard]] vk::Sampler sampler(const SamplerAsset::ID& id);

private:
    AssetManager* assets = nullptr;
    AssetDataManager* data = nullptr;
    VulkanContext* vulkan = nullptr;
    DescriptorManager* descriptors = nullptr;
    ShaderManager* shaders = nullptr;
    vk::raii::CommandPool uploadPool = nullptr;
    std::unordered_map<MeshAsset::ID, std::unique_ptr<GpuMesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<GpuMaterial>> materials;
    std::unordered_map<TextureAsset::ID, std::shared_ptr<GpuTexture>> textures;
    std::unordered_map<SamplerAsset::ID, vk::raii::Sampler> samplers;

    [[nodiscard]] GpuUploadContext uploadContext() const;
};
