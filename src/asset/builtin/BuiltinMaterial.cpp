#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] PbrMaterialAsset makeWhiteMaterial()
{
    ::PbrMaterialAsset asset{};
    asset.id               = BuiltinAssets::Material::white;
    asset.metallic         = 0.0f;
    asset.roughness        = 0.4f;
    asset.baseColorTexture = TextureBinding{
        BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat};
    return asset;
}
}

const ::MaterialAsset* BuiltinAssets::materialAsset(const ::MaterialAsset::ID& id)
{
    static const ::PbrMaterialAsset assets[] = {
        makeWhiteMaterial(),
    };

    for (const ::MaterialAsset& asset : assets)
    {
        if (asset.id == id)
        {
            return &asset;
        }
    }
    return nullptr;
}
