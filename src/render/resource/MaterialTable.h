#pragma once

#include "asset/AssetID.h"
#include "render/RenderConfig.h"
#include "render/resource/Buffer.h"
#include "render/resource/MaterialData.h"

#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

class Device;
class AssetManager;
class RenderResourceManager;
class VulkanContext;
struct TextureAsset;
struct SamplerAsset;
struct MaterialAsset;
struct PbrMaterialAsset;
struct ShaderAsset;
struct TextureBinding;

// Stable asset indices shared by all frames; descriptor sets are updated after their frame fence completes.
class MaterialTable
{
public:
    struct MaterialInfo
    {
        AssetID<ShaderAsset> shaderId;
        uint32_t             alphaMode;
        bool                 doubleSided;
        bool                 clearcoat;
        bool                 sheen;
    };

    struct OverrideOwner
    {
        uint32_t objectId;
        uint32_t instanceIndex;
        uint32_t submeshIndex;

        bool operator==(const OverrideOwner&) const = default;
    };
    struct MaterialKey
    {
        AssetID<MaterialAsset>       assetId;
        std::optional<OverrideOwner> overrideOwner;

        bool operator==(const MaterialKey&) const = default;
    };

    void     init(RenderResourceManager& resources, const VulkanContext& vulkan, Device& device);
    uint32_t update(
        const MaterialKey& key, const PbrMaterialAsset& material, RenderResourceManager& resources, AssetManager& assets
    );
    void                              upload(const VulkanContext& vulkan, uint32_t frameIndex);
    [[nodiscard]] const MaterialInfo& info(uint32_t materialIndex) const { return materialInfos[materialIndex]; }
    [[nodiscard]] vk::DescriptorSet   descriptorSetHandle(uint32_t frameIndex) const
    {
        return *frames[frameIndex].descriptorSet;
    }

private:
    TextureRef texture(const TextureBinding& binding, RenderResourceManager& resources);

    struct MaterialKeyHash
    {
        size_t operator()(const MaterialKey& key) const noexcept;
    };
    struct Frame
    {
        Buffer                  materialBuffer;
        vk::raii::DescriptorSet descriptorSet = nullptr;
        uint32_t                textureCount  = 0;
        uint32_t                samplerCount  = 0;
    };
    std::array<Frame, maxFramesInFlight>                       frames;
    std::vector<MaterialData>                                  materials;
    std::vector<MaterialInfo>                                  materialInfos;
    std::unordered_map<MaterialKey, uint32_t, MaterialKeyHash> materialIndices;
    std::unordered_map<AssetID<TextureAsset>, uint32_t>        textureIndices;
    std::unordered_map<AssetID<SamplerAsset>, uint32_t>        samplerIndices;
    std::array<vk::DescriptorImageInfo, kMaxMaterialTextures>  images;
    std::array<vk::DescriptorImageInfo, kMaxMaterialSamplers>  samplers;
};
