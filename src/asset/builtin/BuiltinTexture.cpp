#include "asset/BuiltinAssets.h"
#include "core/Logger.h"

#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stb_image.h>

namespace
{
constexpr uint8_t kBrdfLutPng[] = {
#embed "data/lut_ggx.png"
};
constexpr uint8_t kSheenLutPng[] = {
#embed "data/lut_sheen_E.png"
};

[[nodiscard]] ::TextureAsset
makeTexture(const ::TextureAsset::ID& id, ImageLayout layout, ColorSpace colorSpace = ColorSpace::Srgb)
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

struct LutTexture
{
    ::TextureAsset       asset;
    std::vector<uint8_t> bytes;
};

LutTexture loadLut(const ::TextureAsset::ID& id, std::span<const uint8_t> encoded, ImageFormat format)
{
    int        width     = 0;
    int        height    = 0;
    int        channels  = 0;
    const auto byteCount = static_cast<int>(encoded.size());
    LutTexture lut{.asset = makeTexture(id, ImageLayout::Image2D, ColorSpace::Linear)};

    if (format == ImageFormat::RG8)
    {
        CHECK(!stbi_is_16_bit_from_memory(encoded.data(), byteCount), "expected an 8-bit BRDF LUT: {}", id);
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded{
            stbi_load_from_memory(encoded.data(), byteCount, &width, &height, &channels, 3),
            stbi_image_free
        };
        CHECK(
            decoded && width > 0 && height > 0 && channels == 3,
            "cannot decode builtin LUT '{}': {}",
            id,
            stbi_failure_reason()
        );
        const auto pixelCount = static_cast<size_t>(width) * height;
        lut.bytes.resize(pixelCount * 2);
        for (size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            lut.bytes[pixel * 2]     = decoded.get()[pixel * 3];
            lut.bytes[pixel * 2 + 1] = decoded.get()[pixel * 3 + 1];
        }
    }
    else
    {
        DCHECK(format == ImageFormat::RGBA32F);
        CHECK(stbi_is_16_bit_from_memory(encoded.data(), byteCount), "expected a 16-bit Sheen LUT: {}", id);
        const std::unique_ptr<stbi_us, decltype(&stbi_image_free)> decoded{
            stbi_load_16_from_memory(encoded.data(), byteCount, &width, &height, &channels, 4),
            stbi_image_free
        };
        CHECK(
            decoded && width > 0 && height > 0 && channels == 3,
            "cannot decode builtin LUT '{}': {}",
            id,
            stbi_failure_reason()
        );
        std::vector<float> pixels(static_cast<size_t>(width) * height * 4);
        for (size_t index = 0; index < pixels.size(); ++index)
            pixels[index] = static_cast<float>(decoded.get()[index]) / std::numeric_limits<stbi_us>::max();
        lut.bytes.resize(pixels.size() * sizeof(float));
        std::memcpy(lut.bytes.data(), pixels.data(), lut.bytes.size());
    }

    lut.asset.format = format;
    lut.asset.width  = static_cast<uint32_t>(width);
    lut.asset.height = static_cast<uint32_t>(height);
    return lut;
}

const LutTexture* findLut(const ::TextureAsset::ID& id)
{
    if (id == BuiltinAssets::Texture::kBrdfLut)
    {
        static const auto kLut = loadLut(id, kBrdfLutPng, ImageFormat::RG8);
        return &kLut;
    }
    if (id == BuiltinAssets::Texture::kSheenLut)
    {
        static const auto kLut = loadLut(id, kSheenLutPng, ImageFormat::RGBA32F);
        return &kLut;
    }
    return nullptr;
}
}  // namespace

const ::TextureAsset* BuiltinAssets::textureAsset(const ::TextureAsset::ID& id)
{
    if (const auto* lut = findLut(id))
        return &lut->asset;

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
    if (const auto* lut = findLut(id))
        return &lut->bytes;

    struct Entry
    {
        ::TextureAsset::ID   id;
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
