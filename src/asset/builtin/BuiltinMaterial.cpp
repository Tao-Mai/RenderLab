#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] StandardMaterial makeWhiteMaterial()
{
    ::StandardMaterial asset{};
    asset.id               = BuiltinAssets::Material::white;
    asset.specularParams   = {0.0f, 0.4f, 0.0f, 1.0f};

    return asset;
}
}

const ::StandardMaterial* BuiltinAssets::materialAsset(const ::StandardMaterial::ID& id)
{
    static const ::StandardMaterial assets[] = {
        makeWhiteMaterial(),
    };

    for (const ::StandardMaterial& asset : assets)
    {
        if (asset.id == id)
        {
            return &asset;
        }
    }
    return nullptr;
}
