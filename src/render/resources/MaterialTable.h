#pragma once
#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetID.h"
#include "render/RenderConfig.h"
#include "render/device/GpuResources.h"
#include "render/resources/MaterialData.h"

#include <array>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>


class AssetManager;
class RenderResourceManager;
struct TextureAsset;
struct SamplerAsset;
struct MaterialAsset;
struct GltfMaterial;
struct TextureBinding;

// Stable asset indices and a shared SSBO; descriptor sets remain per frame in flight.
class MaterialTable
{
public:
    struct MaterialInfo
    {
        uint32_t alphaMode;
        bool     doubleSided;
        bool     clearcoat;
        bool     sheen;
        bool     emissive;
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

    void     init(RenderResourceManager& resources);
    void     setup(GpuResources& cache, rhi::IDevice& device);
    uint32_t update(
        const MaterialKey& key, const GltfMaterial& material, RenderResourceManager& resources, AssetManager& assets
    );
    void                              upload();
    [[nodiscard]] TextureRef          textureReference(const TextureBinding& binding, RenderResourceManager& resources);
    [[nodiscard]] const MaterialInfo& info(uint32_t materialIndex) const { return materialInfos[materialIndex]; }
    [[nodiscard]] uint64_t            revision() const { return materialRevision; }
    [[nodiscard]] GpuResources::BufferHandle bufferHandle() const { return materialBuffer; }
    void                                     bind(rhi::IShaderObject* object) const;

private:
    struct MaterialKeyHash
    {
        size_t operator()(const MaterialKey& key) const noexcept;
    };
    GpuResources*                                                    cache = nullptr;
    GpuResources::BufferHandle                                       materialBuffer;
    std::vector<MaterialData>                                        materials;
    uint64_t                                                         materialRevision = 0;
    std::vector<MaterialInfo>                                        materialInfos;
    std::unordered_map<MaterialKey, uint32_t, MaterialKeyHash>       materialIndices;
    std::unordered_map<AssetID<TextureAsset>, uint32_t>              textureIndices;
    std::unordered_map<AssetID<SamplerAsset>, uint32_t>              samplerIndices;
    std::array<rhi::ComPtr<rhi::ITextureView>, kMaxMaterialTextures> images;
    std::array<rhi::ComPtr<rhi::ISampler>, kMaxMaterialSamplers>     samplers;
};
