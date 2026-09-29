#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] ::Sampler::Desc makeSampler(
    const ::Sampler::ID& id, ::Sampler::Desc::Filter filter,
    ::Sampler::Desc::AddressMode addressMode)
{
    ::Sampler::Desc desc{};
    desc.id           = id;
    desc.magFilter    = filter;
    desc.minFilter    = filter;
    desc.addressModeU = addressMode;
    desc.addressModeV = addressMode;
    desc.addressModeW = addressMode;
    return desc;
}
}

const ::Sampler::Desc* BuiltinAssets::samplerDesc(const ::Sampler::ID& id)
{
    using Filter = ::Sampler::Desc::Filter;
    using AddressMode = ::Sampler::Desc::AddressMode;

    static const ::Sampler::Desc descs[] = {
        makeSampler(Sampler::linearRepeat, Filter::Linear, AddressMode::Repeat),
        makeSampler(Sampler::linearClamp, Filter::Linear, AddressMode::ClampToEdge),
        makeSampler(Sampler::nearestRepeat, Filter::Nearest, AddressMode::Repeat),
        makeSampler(Sampler::nearestClamp, Filter::Nearest, AddressMode::ClampToEdge),
    };

    for (const ::Sampler::Desc& desc : descs)
    {
        if (desc.id == id)
        {
            return &desc;
        }
    }
    return nullptr;
}
