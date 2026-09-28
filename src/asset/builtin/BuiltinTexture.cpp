#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] ::Texture::Desc makeTexture(const ::Texture::ID& id, ImageLayout layout)
{
    ::Texture::Desc desc{};
    desc.id         = id;
    desc.source     = Source::Builtin;
    desc.format     = ImageFormat::RGBA8;
    desc.colorSpace = ColorSpace::Srgb;
    desc.layout     = layout;
    desc.width      = 1;
    desc.height     = 1;
    desc.mipLevels  = 1;
    return desc;
}
}

const ::Texture::Desc* BuiltinAssets::textureDesc(const ::Texture::ID& id)
{
    static const ::Texture::Desc descs[] = {
        makeTexture(Texture::white, ImageLayout::Image2D),
        makeTexture(Texture::whiteCube, ImageLayout::Cubemap),
    };

    for (const ::Texture::Desc& desc : descs)
    {
        if (desc.id == id)
        {
            return &desc;
        }
    }
    return nullptr;
}

const std::vector<uint8_t>* BuiltinAssets::textureData(const ::Texture::ID& id)
{
    struct Entry
    {
        ::Texture::ID          id;
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
