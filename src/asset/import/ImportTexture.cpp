#include "asset/AssetImporter.h"

#include "asset/Asset.h"
#include "asset/AssetManager.h"
#include "asset/AssetDataManager.h"
#include "asset/import/ImportId.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <glm/gtc/packing.hpp>

TextureAsset::ID AssetImporter::saveTexture(
    TextureAsset                  asset,
    const std::filesystem::path& source,
    std::span<const uint8_t>     bytes)
{
    asset.path   = asset_import::sourcePathForAsset(source).generic_string();
    LOG_INFO("Writing texture '{}' ({}x{}, {} mips, {} bytes)",
             asset.id.value, asset.width, asset.height, asset.mipLevels, bytes.size());
    asset.binary = context().assetDataManager->writeTexture(asset, bytes);
    const TextureAsset::ID id = context().assetManager->save<TextureAsset>(std::move(asset));
    LOG_INFO("Texture '{}' saved", id.value);
    return id;
}

TextureAsset::ID AssetImporter::importTexture(const ImportTextureSetting& setting)
{
    const std::filesystem::path& source = setting.source;
    CHECK(!source.empty(), "texture source path is empty");
    const auto start = std::chrono::steady_clock::now();
    LOG_INFO("Import texture '{}' started", source.string());
    LoadedImage image = loadImage(source);
    LOG_INFO("Texture source decoded: {}x{}", image.width, image.height);

    TextureAsset asset{};
    asset.id         = asset_import::nextId<TextureAsset>(source);
    asset.source     = Source::File;
    asset.format     = image.format;
    asset.colorSpace = setting.colorSpace.value_or(defaultColorSpace(image.fileFormat));
    CHECK((image.format == ImageFormat::RGBA16F || image.format == ImageFormat::RGBA32F)
          ? asset.colorSpace == ColorSpace::Linear : true,
          "floating-point textures require linear color space: {}",
          source.string());
    asset.layout    = ImageLayout::Image2D;
    asset.width     = image.width;
    asset.height    = image.height;
    asset.mipLevels = 1;

    std::vector<uint8_t> bytes;
    if (auto* rgba8 = std::get_if<std::vector<uint8_t>>(&image.pixels))
    {
        bytes = std::move(*rgba8);
    }
    else
    {
        const auto&    rgba       = std::get<std::vector<float>>(image.pixels);
        const uint64_t pixelCount = static_cast<uint64_t>(image.width) * image.height;
        const uint64_t byteCount  = pixelCount * AssetDataManager::bytesPerPixel(image.format);
        CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
              "imported image is too large: {}",
              source.string());
        bytes.resize(static_cast<size_t>(byteCount));
        if (image.format == ImageFormat::RGBA16F)
        {
            for (size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                const std::array<uint16_t, 4> half{
                    glm::packHalf1x16(rgba[pixel * 4]),
                    glm::packHalf1x16(rgba[pixel * 4 + 1]),
                    glm::packHalf1x16(rgba[pixel * 4 + 2]),
                    glm::packHalf1x16(rgba[pixel * 4 + 3]),
                };
                std::memcpy(bytes.data() + pixel * sizeof(half),
                            half.data(),
                            sizeof(half));
            }
        }
        else
        {
            CHECK(image.format == ImageFormat::RGBA32F,
                  "invalid floating-point image format: {}",
                  source.string());
            std::memcpy(bytes.data(), rgba.data(), bytes.size());
        }
    }
    const TextureAsset::ID id = saveTexture(std::move(asset), source, bytes);
    LOG_INFO("Import texture '{}' completed as '{}' in {:.1f}s",
             source.string(), id.value,
             std::chrono::duration<double>(
                 std::chrono::steady_clock::now() - start).count());
    return id;
}
