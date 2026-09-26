#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] TextureDesc makeTexture(const AssetId& id, ImageLayout layout)
{
    TextureDesc desc{};
    desc.id         = id;
    desc.source     = Source::Builtin;
    desc.format     = ImageFormat::RGBA8;
    desc.colorSpace = ColorSpace::Srgb;
    desc.layout     = layout;
    desc.width      = 1;
    desc.height     = 1;
    return desc;
}
}

const TextureDesc* BuiltinAssets::textureDesc(const AssetId& id)
{
    static const TextureDesc descs[] = {
        makeTexture(Texture::white, ImageLayout::Image2D),
        makeTexture(Texture::whiteCube, ImageLayout::Cubemap),
    };

    for (const TextureDesc& desc : descs)
    {
        if (desc.id == id)
        {
            return &desc;
        }
    }
    return nullptr;
}

const std::vector<uint8_t>* BuiltinAssets::textureData(const AssetId& id)
{
    struct Entry
    {
        AssetId              id;
        std::vector<uint8_t> bytes;
    };

    static const Entry entries[] = {
        {Texture::white, std::vector<uint8_t>(4, 255)},
        {Texture::whiteCube, std::vector<uint8_t>(6 * 4, 255)},
    };

    for (const Entry& entry : entries)
    {
        if (entry.id == id)
        {
            return &entry.bytes;
        }
    }
    return nullptr;
}
