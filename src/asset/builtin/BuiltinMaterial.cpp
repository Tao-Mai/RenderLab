#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] MaterialDesc makeWhiteMaterial()
{
    MaterialDesc desc{};
    desc.id               = BuiltinAssets::Material::white;
    desc.metallic         = 0.0f;
    desc.roughness        = 0.4f;
    desc.baseColorTexture = BuiltinAssets::Texture::white;
    return desc;
}
}

const MaterialDesc* BuiltinAssets::materialDesc(const AssetId& id)
{
    static const MaterialDesc descs[] = {
        makeWhiteMaterial(),
    };

    for (const MaterialDesc& desc : descs)
    {
        if (desc.id == id)
        {
            return &desc;
        }
    }
    return nullptr;
}
