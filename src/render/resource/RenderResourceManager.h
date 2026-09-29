#pragma once

#include "asset/Asset.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuTexture.h"
#include "render/device/GpuUploadContext.h"

#include <memory>
#include <unordered_map>

#include <vulkan/vulkan_raii.hpp>

class AssetDescManager;
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

    GpuMesh& mesh(const Mesh::ID& id);
    [[nodiscard]] Material::Desc materialDesc(const Material::ID& id) const;
    GpuMaterial& material(const Material::ID& id);
    GpuMaterial& material(const Material::Desc& desc);
    std::shared_ptr<GpuTexture> texture(const Texture::ID& id);
    [[nodiscard]] vk::Sampler sampler(const Sampler::ID& id);

private:
    AssetDescManager* assets = nullptr;
    AssetDataManager* data = nullptr;
    VulkanContext* vulkan = nullptr;
    DescriptorManager* descriptors = nullptr;
    ShaderManager* shaders = nullptr;
    vk::raii::CommandPool uploadPool = nullptr;
    std::unordered_map<Mesh::ID, std::unique_ptr<GpuMesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<GpuMaterial>> materials;
    std::unordered_map<Texture::ID, std::shared_ptr<GpuTexture>> textures;
    std::unordered_map<Sampler::ID, vk::raii::Sampler> samplers;

    [[nodiscard]] GpuUploadContext uploadContext() const;
};
