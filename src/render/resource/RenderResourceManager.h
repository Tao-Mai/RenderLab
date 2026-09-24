#pragma once

#include "asset/AssetId.h"
#include "asset/AssetDesc.h"
#include "render/resource/Material.h"
#include "render/resource/Mesh.h"
#include "render/resource/Texture.h"
#include "render/device/GpuUploadContext.h"

#include <memory>
#include <unordered_map>

#include <vulkan/vulkan_raii.hpp>

class AssetManager;
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

    Mesh& mesh(const AssetId& id);
    [[nodiscard]] MaterialDesc materialDesc(const AssetId& id) const;
    Material& material(const AssetId& id);
    Material& material(const MaterialDesc& desc);
    std::shared_ptr<Texture> texture(const AssetId& id);

private:
    AssetManager* assets = nullptr;
    VulkanContext* vulkan = nullptr;
    DescriptorManager* descriptors = nullptr;
    ShaderManager* shaders = nullptr;
    vk::raii::CommandPool uploadPool = nullptr;
    std::unordered_map<AssetId, std::unique_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
    std::unordered_map<AssetId, std::shared_ptr<Texture>> textures;

    [[nodiscard]] GpuUploadContext uploadContext() const;
};
