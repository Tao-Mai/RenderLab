#include "render/resources/RenderResourceManager.h"
#include "render/resources/TextureUpload.h"
#include <cmath>
#include <limits>
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "render/RhiUtils.h"
namespace
{
rhi::TextureAddressingMode addressMode(SamplerAsset::AddressMode mode)
{
    switch (mode)
    {
        case SamplerAsset::AddressMode::Repeat:
            return rhi::TextureAddressingMode::Wrap;
        case SamplerAsset::AddressMode::MirroredRepeat:
            return rhi::TextureAddressingMode::MirrorRepeat;
        case SamplerAsset::AddressMode::ClampToEdge:
            return rhi::TextureAddressingMode::ClampToEdge;
        case SamplerAsset::AddressMode::ClampToBorder:
            return rhi::TextureAddressingMode::ClampToBorder;
    }
    LOG_FATAL("invalid sampler address mode");
}
}  // namespace

RenderResourceManager::~RenderResourceManager() = default;
void RenderResourceManager::init(rhi::IDevice& target, const AssetManager& targetAssets)
{
    device = &target;
    assets = &targetAssets;
}

void RenderResourceManager::shutdown() noexcept
{
    environmentMaps.clear();
    meshes.clear();
    textures.clear();
    samplers.clear();
    device = nullptr;
    assets = nullptr;
}

const GpuMesh& RenderResourceManager::loadMesh(const MeshAsset::ID& id, rhi::ICommandEncoder& commands)
{
    auto& versions = meshes[id];
    const auto revision = assets->revision(id);
    if (const auto found = versions.find(revision); found != versions.end())
        return found->second;

    const auto& meshAsset = assets->get(id);
    const auto geometry = assets->readGeometry(meshAsset);

    return versions.try_emplace(revision, *device, commands, geometry, meshAsset.submeshes).first->second;
}

rhi::ComPtr<rhi::ITexture> RenderResourceManager::texture(const TextureAsset::ID& id, rhi::ICommandEncoder& commands)
{
    if (const auto found = textures.find(id); found != textures.end())
    {
        return found->second;
    }

    const TextureAsset& asset = assets->get<TextureAsset>(id);

    const std::vector<uint8_t> bytes = assets->readTexture(asset);
    auto                       gpu   = uploadTexture(*device, commands, asset, bytes);

    return textures.emplace(id, std::move(gpu)).first->second;
}

const GpuEnvironmentMap& RenderResourceManager::environmentMap(
    const EnvironmentMapAsset::ID& id, rhi::ICommandEncoder& commands
)
{
    if (const auto found = environmentMaps.find(id); found != environmentMaps.end())
        return found->second;

    const auto& environment = assets->get<EnvironmentMapAsset>(id);
    CHECK(!environment.radiance.textureID.empty() && !environment.radiance.samplerID.empty(),
          "environment '{}' requires a radiance texture and sampler", id);
    
    const auto& radiance = assets->get(environment.radiance.textureID);
    CHECK(radiance.layout == ImageLayout::Cubemap,
          "environment '{}' requires a radiance cubemap", id);

    GpuEnvironmentMap result;
    rhiCheck(texture(environment.radiance.textureID, commands)->getDefaultView(result.radiance.writeRef()));
    result.radianceSampler = sampler(environment.radiance.samplerID);
    return environmentMaps.emplace(id, std::move(result)).first->second;
}

void GpuEnvironmentMap::bindRadiance(const rhi::ShaderCursor& root) const
{
    rhiCheck(root["radianceMap"].setBinding(radiance));
    rhiCheck(root["radianceSampler"].setBinding(radianceSampler));
}

rhi::ISampler* RenderResourceManager::sampler(const SamplerAsset::ID& id)
{
    if (const auto found = samplers.find(id); found != samplers.end())
        return found->second;
    
    const auto& asset = assets->get<SamplerAsset>(id);
    
    CHECK(
        std::isfinite(asset.mipLodBias) && std::isfinite(asset.maxAnisotropy) && std::isfinite(asset.minLod) &&
            (!asset.maxLod || (std::isfinite(*asset.maxLod) && asset.minLod <= *asset.maxLod)),
        "invalid sampler: {}",
        id
    );
    CHECK(!asset.unnormalizedCoordinates, "slang-rhi requires normalized sampler coordinates: {}", id);
    
    rhi::SamplerDesc desc;
    desc.minFilter      = asset.minFilter == SamplerAsset::Filter::Linear ? rhi::TextureFilteringMode::Linear
                                                                          : rhi::TextureFilteringMode::Point;
    desc.magFilter      = asset.magFilter == SamplerAsset::Filter::Linear ? rhi::TextureFilteringMode::Linear
                                                                          : rhi::TextureFilteringMode::Point;
    desc.mipFilter      = asset.mipmapMode == SamplerAsset::MipmapMode::Linear ? rhi::TextureFilteringMode::Linear
                                                                               : rhi::TextureFilteringMode::Point;
    desc.addressU       = addressMode(asset.addressModeU);
    desc.addressV       = addressMode(asset.addressModeV);
    desc.addressW       = addressMode(asset.addressModeW);
    desc.mipLODBias     = asset.mipLodBias;
    desc.maxAnisotropy  = asset.anisotropyEnable ? uint32_t(asset.maxAnisotropy) : 1;
    desc.minLOD         = asset.minLod;
    desc.maxLOD         = asset.maxLod.value_or(std::numeric_limits<float>::max());
    desc.reductionOp    = asset.compareEnable ? rhi::TextureReductionOp::Comparison : rhi::TextureReductionOp::Average;
    desc.comparisonFunc = static_cast<rhi::ComparisonFunc>(asset.compareOp);
    const bool white    = asset.borderColor == SamplerAsset::BorderColor::FloatOpaqueWhite ||
        asset.borderColor == SamplerAsset::BorderColor::IntOpaqueWhite;
    const bool opaque = white || asset.borderColor == SamplerAsset::BorderColor::FloatOpaqueBlack ||
        asset.borderColor == SamplerAsset::BorderColor::IntOpaqueBlack;
    desc.borderColor[0] = desc.borderColor[1] = desc.borderColor[2] = white ? 1.0f : 0.0f;
    desc.borderColor[3]                                             = opaque ? 1.0f : 0.0f;
    rhi::ComPtr<rhi::ISampler> sampler;
    rhiCheck(device->createSampler(desc, sampler.writeRef()));
    return samplers.emplace(id, sampler).first->second;
}
