#pragma once

#include "asset/MeshAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/TextureAsset.h"

#include "render/resources/GpuMesh.h"
#include "render/resources/TextureUpload.h"

#include <memory>
#include <unordered_map>

#include <slang-rhi.h>

class AssetManager;
class AssetDataManager;
class RenderResourceManager
{
public:
    ~RenderResourceManager();

    void init(rhi::IDevice& device, AssetManager& assets, AssetDataManager& data);
    void shutdown() noexcept;

    GpuMesh&                     mesh(const MeshAsset::ID& id);
    rhi::ComPtr<rhi::ITexture>   texture(const TextureAsset::ID& id);
    [[nodiscard]] rhi::ISampler* sampler(const SamplerAsset::ID& id);

private:
    AssetManager*                                                    assets = nullptr;
    AssetDataManager*                                                data   = nullptr;
    rhi::IDevice*                                                    device = nullptr;
    std::unordered_map<MeshAsset::ID, std::unique_ptr<GpuMesh>>      meshes;
    std::unordered_map<TextureAsset::ID, rhi::ComPtr<rhi::ITexture>> textures;
    std::unordered_map<SamplerAsset::ID, rhi::ComPtr<rhi::ISampler>> samplers;
};
