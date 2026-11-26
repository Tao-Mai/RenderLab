#pragma once
#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetID.h"
#include "render/resources/StandardMaterialData.h"

#include <array>
#include <optional>
#include <unordered_map>
#include <vector>


class AssetManager;
class RenderResourceManager;
struct TextureAsset;
struct SamplerAsset;
struct StandardMaterial;
struct TextureBinding;

// Material and texture indices are stable within a scene. Shader objects share immutable texture bindings.
class MaterialTable
{
public:
    struct MaterialInfo
    {
        bool emissive;
    };

    void init(rhi::IDevice& device, RenderResourceManager& resources);
    void reset();
    [[nodiscard]] uint32_t resolve(const AssetID<StandardMaterial>& id, const AssetManager& assets);
    [[nodiscard]] bool upload(rhi::ICommandEncoder& commands);
    [[nodiscard]] const MaterialInfo& info(uint32_t materialIndex) const { return materialInfos[materialIndex]; }
    [[nodiscard]] const StandardMaterialData& data(uint32_t materialIndex) const { return materials[materialIndex]; }
    [[nodiscard]] uint64_t emissionRevision() const { return emissiveRevision; }
    void bind(rhi::IShaderObject& object) const;

private:
    rhi::IDevice*                        device = nullptr;
    RenderResourceManager*              resources = nullptr;
    rhi::ComPtr<rhi::IBuffer>            materialBuffer;
    std::optional<uint64_t>              uploadedMaterialRevision;
    std::vector<StandardMaterialData>    materials;
    uint64_t                            materialRevision = 0;
    uint64_t                            emissiveRevision = 0;
    std::vector<MaterialInfo>            materialInfos;

    std::unordered_map<AssetID<StandardMaterial>, uint32_t> materialIndices;
    std::unordered_map<AssetID<TextureAsset>, uint32_t>     textureIndices;
    std::vector<AssetID<TextureAsset>>                     pendingImages;
    std::unordered_map<AssetID<SamplerAsset>, uint32_t>     samplerIndices;
    std::array<rhi::ComPtr<rhi::ITextureView>, kMaxMaterialTextures> images;
    std::array<rhi::ComPtr<rhi::ISampler>, kMaxMaterialSamplers>     samplers;
    mutable std::unordered_map<slang::TypeLayoutReflection*, rhi::ComPtr<rhi::IShaderObject>> textureBindings;

    [[nodiscard]] TextureRef textureReference(const TextureBinding& binding);
    uint32_t store(const AssetID<StandardMaterial>& id, const StandardMaterialData& data, MaterialInfo info);
};
