#pragma once

#include <array>
#include <map>
#include "asset/MeshAsset.h"
#include "asset/PbrMaterialAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/TextureAsset.h"
#include "render/RenderConfig.h"
#include "render/RenderData.h"
#include "render/device/GpuUploadContext.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuTexture.h"
#include "render/resource/MaterialTable.h"

#include <memory>
#include <unordered_map>

#include <vulkan/vulkan_raii.hpp>

struct SceneRenderData;
struct PreparedScene;
class AssetManager;
class AssetDataManager;
class Device;
class ShaderManager;
class VulkanContext;

class RenderResourceManager
{
public:
    ~RenderResourceManager();

    void
    init(VulkanContext& vulkan, Device& device, ShaderManager& shaders, AssetManager& assets, AssetDataManager& data);
    void shutdown() noexcept;

    GpuMesh&                     mesh(const MeshAsset::ID& id);
    std::shared_ptr<GpuMaterial> material(const MaterialAsset::ID& id);
    // The caller must wait for this frame's fence before preparing its resources.
    const RenderData& prepare(const SceneRenderData& scene, const PreparedScene& prepared, uint32_t frameIndex);
    std::shared_ptr<GpuTexture> texture(const TextureAsset::ID& id);
    [[nodiscard]] vk::Sampler   sampler(const SamplerAsset::ID& id);

private:
    AssetManager*                                               assets     = nullptr;
    AssetDataManager*                                           data       = nullptr;
    VulkanContext*                                              vulkan     = nullptr;
    Device*                                                     device     = nullptr;
    ShaderManager*                                              shaders    = nullptr;
    vk::raii::CommandPool                                       uploadPool = nullptr;
    std::unordered_map<MeshAsset::ID, std::unique_ptr<GpuMesh>> meshes;
    struct SharedMaterial
    {
        uint64_t                     revision = 0;
        std::shared_ptr<GpuMaterial> gpu;
    };
    struct MaterialInstance
    {
        PbrMaterialAsset             asset;
        std::shared_ptr<GpuMaterial> gpu;
        bool                         used = false;
    };
    struct FrameResources
    {
        RenderData                                                           draws;
        std::map<std::tuple<uint32_t, uint32_t, uint32_t>, MaterialInstance> instances;
        MaterialTable                                                        materials;
    };
    std::array<FrameResources, maxFramesInFlight>                     frameResources;
    std::unordered_map<MaterialAsset::ID, SharedMaterial>             materials;
    std::shared_ptr<GpuMaterial>                                      createMaterial(const PbrMaterialAsset& asset);
    std::unordered_map<TextureAsset::ID, std::shared_ptr<GpuTexture>> textures;
    std::unordered_map<SamplerAsset::ID, vk::raii::Sampler>           samplers;

    [[nodiscard]] GpuUploadContext uploadContext() const;
};
