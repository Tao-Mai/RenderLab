#include "asset/BuiltinAssets.h"

namespace
{
[[nodiscard]] ::TextureAsset makeTexture(
    const ::TextureAsset::ID& id, ImageLayout layout, ColorSpace colorSpace = ColorSpace::Srgb)
{
    ::TextureAsset asset{};
    asset.id         = id;
    asset.source     = Source::Builtin;
    asset.format     = ImageFormat::RGBA8;
    asset.colorSpace = colorSpace;
    asset.layout     = layout;
    asset.width      = 1;
    asset.height     = 1;
    asset.mipLevels  = 1;
    return asset;
}
}

const ::TextureAsset* BuiltinAssets::textureAsset(const ::TextureAsset::ID& id)
{
    static const ::TextureAsset assets[] = {
        makeTexture(Texture::white, ImageLayout::Image2D),
        makeTexture(Texture::whiteCube, ImageLayout::Cubemap),
        makeTexture(Texture::flatNormal, ImageLayout::Image2D, ColorSpace::Linear),
        makeTexture(Texture::kWhiteLinear, ImageLayout::Image2D, ColorSpace::Linear),
    };

    for (const ::TextureAsset& asset : assets)
    {
        if (asset.id == id)
        {
            return &asset;
        }
    }
    return nullptr;
}

const std::vector<uint8_t>* BuiltinAssets::textureData(const ::TextureAsset::ID& id)
{
    struct Entry
    {
        ::TextureAsset::ID          id;
        std::vector<uint8_t> bytes;
    };

    static const Entry entries[] = {
        {Texture::white, std::vector<uint8_t>(4, 255)},
        {Texture::whiteCube, std::vector<uint8_t>(6 * 4, 255)},
        {Texture::flatNormal, {128, 128, 255, 255}},
        {Texture::kWhiteLinear, std::vector<uint8_t>(4, 255)},
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
