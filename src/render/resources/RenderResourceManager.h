#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/TextureAsset.h"

#include "render/resources/GpuMesh.h"

#include <map>
#include <unordered_map>

#include <slang-rhi.h>

class AssetManager;
namespace rhi
{
struct ShaderCursor;
}

struct GpuEnvironmentMap
{
    rhi::ComPtr<rhi::ITextureView> radiance;
    rhi::ISampler*                 radianceSampler = nullptr;

    void bind(const rhi::ShaderCursor& cursor) const;
};

class RenderResourceManager
{
public:
    ~RenderResourceManager();

    void init(rhi::IDevice& device, const AssetManager& assets);
    void shutdown() noexcept;

    [[nodiscard]] const GpuMesh& loadMesh(const MeshAsset::ID& id, rhi::ICommandEncoder& commands);
    rhi::ComPtr<rhi::ITexture>   texture(const TextureAsset::ID& id, rhi::ICommandEncoder& commands);
    [[nodiscard]] rhi::ISampler* sampler(const SamplerAsset::ID& id);
    [[nodiscard]] const GpuEnvironmentMap& environmentMap(const EnvironmentMapAsset::ID& id,
        rhi::ICommandEncoder& commands);

private:
    const AssetManager*                                              assets = nullptr;
    rhi::IDevice*                                                    device = nullptr;
    // In-flight ray dispatches use device addresses from the mesh version they recorded.
    std::unordered_map<MeshAsset::ID, std::map<uint64_t, GpuMesh>>    meshes;
    std::unordered_map<TextureAsset::ID, rhi::ComPtr<rhi::ITexture>> textures;
    std::unordered_map<SamplerAsset::ID, rhi::ComPtr<rhi::ISampler>> samplers;
    std::unordered_map<EnvironmentMapAsset::ID, GpuEnvironmentMap> environmentMaps;
};
