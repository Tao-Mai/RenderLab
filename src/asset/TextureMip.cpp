#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include "asset/TextureMip.h"

#include "asset/Asset.h"
#include "asset/AssetDataManager.h"
#include "core/Logger.h"

#include <cstring>
#include <limits>

namespace texture_mip
{
std::vector<uint8_t> generateChain(const TextureAsset& asset, std::span<const uint8_t> basePixels)
{
    CHECK(asset.layout == ImageLayout::Image2D && asset.width > 0 && asset.height > 0);
    CHECK(asset.width <= std::numeric_limits<int>::max() && asset.height <= std::numeric_limits<int>::max());
    CHECK(asset.mipLevels == maxLevels(asset.width, asset.height));

    const uint32_t pixelBytes = AssetDataManager::bytesPerPixel(asset.format);
    CHECK(basePixels.size() == levelBytes(asset.width, asset.height, 1, pixelBytes, 0));
    const uint64_t byteCount = totalBytes(asset.width, asset.height, 1, pixelBytes, asset.mipLevels);
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(), "texture mip chain is too large: {}", asset.id);

    stbir_pixel_layout layout;
    stbir_datatype     dataType = asset.colorSpace == ColorSpace::Srgb ? STBIR_TYPE_UINT8_SRGB : STBIR_TYPE_UINT8;
    switch (asset.format)
    {
        case ImageFormat::R8:
            layout = STBIR_1CHANNEL;
            break;
        case ImageFormat::RG8:
            layout = STBIR_2CHANNEL;
            break;
        case ImageFormat::RGB8:
            layout = STBIR_RGB;
            break;
        case ImageFormat::RGBA8:
            // Color alpha remains linear and weights RGB to avoid transparent-edge fringes.
            layout = asset.colorSpace == ColorSpace::Srgb ? STBIR_RGBA : STBIR_4CHANNEL;
            break;
        case ImageFormat::RGBA16F:
        case ImageFormat::RGBA32F:
            CHECK(asset.colorSpace == ColorSpace::Linear);
            layout   = STBIR_4CHANNEL;
            dataType = asset.format == ImageFormat::RGBA16F ? STBIR_TYPE_HALF_FLOAT : STBIR_TYPE_FLOAT;
            break;
        default:
            LOG_FATAL("invalid texture format: {}", asset.id);
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(byteCount));
    std::memcpy(pixels.data(), basePixels.data(), basePixels.size());
    for (uint32_t mip = 1; mip < asset.mipLevels; ++mip)
    {
        const auto* previous    = pixels.data() + offset(asset.width, asset.height, 1, pixelBytes, mip - 1);
        auto*       destination = pixels.data() + offset(asset.width, asset.height, 1, pixelBytes, mip);
        CHECK(
            stbir_resize(
                previous,
                static_cast<int>(extent(asset.width, mip - 1)),
                static_cast<int>(extent(asset.height, mip - 1)),
                0,
                destination,
                static_cast<int>(extent(asset.width, mip)),
                static_cast<int>(extent(asset.height, mip)),
                0,
                layout,
                dataType,
                STBIR_EDGE_CLAMP,
                STBIR_FILTER_BOX
            ),
            "failed to generate texture mip {}: {}",
            mip,
            asset.id
        );
    }
    return pixels;
}
}  // namespace texture_mip
