#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] ::Material::Desc makeWhiteMaterial()
{
    ::Material::Desc desc{};
    desc.id               = BuiltinAssets::Material::white;
    desc.metallic         = 0.0f;
    desc.roughness        = 0.4f;
    desc.baseColorTexture = BuiltinAssets::Texture::white;
    return desc;
}
}

const ::Material::Desc* BuiltinAssets::materialDesc(const ::Material::ID& id)
{
    static const ::Material::Desc descs[] = {
        makeWhiteMaterial(),
    };

    for (const ::Material::Desc& desc : descs)
    {
        if (desc.id == id)
        {
            return &desc;
        }
    }
    return nullptr;
}
