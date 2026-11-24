#pragma once
#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetID.h"
#include "render/RenderConfig.h"
#include "render/resources/MaterialData.h"
#include "render/resources/PbrtMaterialProgram.h"

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>


class AssetManager;
class AssetDataManager;
class RenderResourceManager;
struct TextureAsset;
struct SamplerAsset;
struct MaterialAsset;
struct GltfMaterial;
struct TextureBinding;
struct DrawItem;

// Stable material and texture indices. Shader objects share immutable texture bindings.
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

    void init(rhi::IDevice& device, RenderResourceManager& resources);
    [[nodiscard]] std::vector<uint32_t>
         prepare(std::span<const DrawItem> items, const AssetManager& assets, const AssetDataManager& assetData);
    [[nodiscard]] bool upload(rhi::ICommandEncoder& commands);
    [[nodiscard]] TextureRef          textureReference(const TextureBinding& binding);
    [[nodiscard]] const MaterialInfo& info(uint32_t materialIndex) const { return materialInfos[materialIndex]; }
    [[nodiscard]] const MaterialData& data(uint32_t materialIndex) const { return materials[materialIndex]; }
    [[nodiscard]] std::span<const pbrt::MaterialInstruction> materialInstructions() const { return materialCode; }
    [[nodiscard]] uint64_t emissionRevision() const { return emissiveRevision; }
    [[nodiscard]] std::span<const std::string_view> rayTracingShaderIncludes() const
    {
        static constexpr std::string_view kIncludes[]{"material/scattering.slang"};
        return kIncludes;
    }
    void bind(rhi::IShaderObject& object) const;

private:
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

    struct MaterialKeyHash
    {
        size_t operator()(const MaterialKey& key) const noexcept;
    };
    rhi::IDevice*                                                    device = nullptr;
    RenderResourceManager*                                           resources = nullptr;
    rhi::ComPtr<rhi::IBuffer>                                        materialBuffer, materialPrograms, texturePrograms;
    std::optional<uint64_t>                                          uploadedMaterialRevision, uploadedProgramRevision;
    std::vector<MaterialData>                                        materials;
    std::vector<pbrt::MaterialInstruction>                           materialCode;
    std::vector<pbrt::TextureInstruction>                            textureCode;
    uint64_t                                                         materialRevision = 0, programRevision = 0;
    uint64_t                                                         emissiveRevision = 0;
    std::vector<MaterialInfo>                                        materialInfos;
    std::unordered_map<MaterialKey, uint32_t, MaterialKeyHash>       materialIndices;
    std::unordered_map<AssetID<TextureAsset>, uint32_t>              textureIndices;
    std::vector<AssetID<TextureAsset>>                              pendingImages;
    std::unordered_map<AssetID<SamplerAsset>, uint32_t>              samplerIndices;
    std::array<rhi::ComPtr<rhi::ITextureView>, kMaxMaterialTextures> images;
    std::array<rhi::ComPtr<rhi::ISampler>, kMaxMaterialSamplers>     samplers;
    mutable std::unordered_map<slang::TypeLayoutReflection*, rhi::ComPtr<rhi::IShaderObject>> textureBindings;

    uint32_t updateGltf(const MaterialKey& key, const GltfMaterial& material, const AssetManager& assets);
    uint32_t store(const MaterialKey& key, const MaterialData& data, MaterialInfo info);
};
