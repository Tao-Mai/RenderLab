#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] ::SamplerAsset makeSampler(
    const ::SamplerAsset::ID& id, ::SamplerAsset::Filter filter,
    ::SamplerAsset::AddressMode addressMode)
{
    ::SamplerAsset asset{};
    asset.id           = id;
    asset.magFilter    = filter;
    asset.minFilter    = filter;
    asset.addressModeU = addressMode;
    asset.addressModeV = addressMode;
    asset.addressModeW = addressMode;
    return asset;
}
}

const ::SamplerAsset* BuiltinAssets::samplerAsset(const ::SamplerAsset::ID& id)
{
    using Filter = ::SamplerAsset::Filter;
    using AddressMode = ::SamplerAsset::AddressMode;

    static const ::SamplerAsset assets[] = {
        makeSampler(Sampler::linearRepeat, Filter::Linear, AddressMode::Repeat),
        makeSampler(Sampler::linearClamp, Filter::Linear, AddressMode::ClampToEdge),
        makeSampler(Sampler::nearestRepeat, Filter::Nearest, AddressMode::Repeat),
        makeSampler(Sampler::nearestClamp, Filter::Nearest, AddressMode::ClampToEdge),
    };

    for (const ::SamplerAsset& asset : assets)
    {
        if (asset.id == id)
        {
            return &asset;
        }
    }
    return nullptr;
}
