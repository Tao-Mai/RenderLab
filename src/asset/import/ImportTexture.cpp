#include "asset/AssetImporter.h"

#include "asset/AssetDesc.h"
#include "asset/AssetDescManager.h"
#include "asset/TextureIo.h"
#include "asset/import/ImportId.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <glm/gtc/packing.hpp>

namespace
{
[[nodiscard]] std::filesystem::path sourceRelativeToRoot(
    const std::filesystem::path& source)
{
    const auto root = context().config->paths().assets;
    const auto relative = std::filesystem::relative(
        std::filesystem::absolute(source), root);
    CHECK(!relative.empty() && *relative.begin() != "..",
          "texture source is outside asset root: {}",
          source.string());
    return relative;
}

[[nodiscard]] std::filesystem::path binaryPath(const AssetId& id)
{
    CHECK(!id.empty() && !isBuiltin<TextureDesc>(id) &&
          std::filesystem::path{id}.filename() == std::filesystem::path{id} &&
          id != "." && id != "..",
          "invalid imported texture id: {}",
          id);
    return std::filesystem::path{"binary/texture"} / (id + ".bin");
}
}

AssetId AssetImporter::saveTexture(
    TextureDesc desc,
    const std::filesystem::path& source,
    std::span<const uint8_t> bytes)
{
    const auto binary = binaryPath(desc.id);
    desc.path = sourceRelativeToRoot(source).generic_string();
    desc.binary = binary.generic_string();
    texture_io::write(context().assetManager->path(binary), desc, bytes);
    return context().assetManager->save(std::move(desc));
}

AssetId AssetImporter::importTexture(const std::filesystem::path& source)
{
    LoadedImage image = loadImage(source);

    TextureDesc desc{};
    desc.id = asset_import::nextId<TextureDesc>(source);
    desc.source = "file";
    desc.format = image.format;
    desc.layout = TextureDesc::Layout::Image2D;
    desc.width = image.width;
    desc.height = image.height;

    std::vector<uint8_t> bytes;
    if (auto* rgba8 = std::get_if<std::vector<uint8_t>>(&image.pixels))
    {
        bytes = std::move(*rgba8);
    }
    else
    {
        const auto& rgba = std::get<std::vector<float>>(image.pixels);
        const uint64_t pixelCount = static_cast<uint64_t>(image.width) * image.height;
        const uint64_t byteCount = pixelCount * texture_io::bytesPerPixel(image.format);
        CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
              "imported image is too large: {}",
              source.string());
        bytes.resize(static_cast<size_t>(byteCount));
        if (image.format == ImageFormat::Rgba16Float)
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
            CHECK(image.format == ImageFormat::Rgba32Float,
                  "invalid EXR format: {}",
                  source.string());
            std::memcpy(bytes.data(), rgba.data(), bytes.size());
        }
    }
    return saveTexture(std::move(desc), source, bytes);
}
