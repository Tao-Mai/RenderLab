#include "asset/TextureIo.h"

#include "core/Logger.h"

#include <array>
#include <fstream>
#include <limits>

namespace
{
constexpr uint32_t textureMagic   = 0x58455452u; // RTEX
constexpr uint32_t textureVersion = 1;
constexpr uint64_t headerBytes    = 8 * sizeof(uint32_t);

[[nodiscard]] uint32_t layerCount(TextureDesc::Layout layout)
{
    switch (layout)
    {
        case TextureDesc::Layout::Image2D:
            return 1;
        case TextureDesc::Layout::Cubemap:
            return 6;
    }
    LOG_FATAL("invalid texture layout");
}

void validate(
    const TextureDesc&           desc, std::span<const uint8_t> bytes,
    const std::filesystem::path& file)
{
    CHECK(desc.width > 0 && desc.height > 0,
          "invalid texture dimensions: {}",
          file.string());
    CHECK(desc.layout != TextureDesc::Layout::Cubemap ||
          desc.width == desc.height,
          "cubemap faces must be square: {}",
          file.string());
    const uint64_t maxPayload = std::numeric_limits<uint32_t>::max();
    CHECK(desc.width <= maxPayload / desc.height /
          layerCount(desc.layout) / texture_io::bytesPerPixel(desc.format),
          "texture payload is too large: {}",
          file.string());
    const uint64_t expected = static_cast<uint64_t>(desc.width) * desc.height *
        layerCount(desc.layout) * texture_io::bytesPerPixel(desc.format);
    CHECK(expected == bytes.size(),
          "invalid texture payload size: {}",
          file.string());
}
}

namespace texture_io
{
uint32_t bytesPerPixel(ImageFormat format)
{
    switch (format)
    {
        case ImageFormat::Rgba8Srgb:
            return 4;
        case ImageFormat::Rgba16Float:
            return 8;
        case ImageFormat::Rgba32Float:
            return 16;
    }
    LOG_FATAL("invalid texture data format");
}

void write(
    const std::filesystem::path& file, const TextureDesc& desc,
    std::span<const uint8_t>     bytes)
{
    validate(desc, bytes, file);
    std::filesystem::create_directories(file.parent_path());
    std::filesystem::path temporary = file;
    temporary                       += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    CHECK(output, "cannot write texture binary: {}", temporary.string());
    const std::array<uint32_t, 8> header{
        textureMagic, textureVersion,
        static_cast<uint32_t>(desc.layout),
        static_cast<uint32_t>(desc.format),
        desc.width, desc.height, layerCount(desc.layout),
        static_cast<uint32_t>(bytes.size()),
    };
    output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    CHECK(output, "failed writing texture binary: {}", temporary.string());
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    CHECK(!error,
          "failed replacing texture binary '{}': {}",
          file.string(),
          error.message());
}

std::vector<uint8_t> read(
    const std::filesystem::path& file, const TextureDesc& desc)
{
    std::ifstream input(file, std::ios::binary);
    CHECK(input, "cannot open texture binary: {}", file.string());
    std::array<uint32_t, 8> header{};
    input.read(reinterpret_cast<char*>(header.data()), sizeof(header));
    CHECK(input && header[0] == textureMagic && header[1] == textureVersion,
          "invalid texture binary header: {}",
          file.string());
    CHECK(header[2] == static_cast<uint32_t>(desc.layout) &&
          header[3] == static_cast<uint32_t>(desc.format) &&
          header[4] == desc.width && header[5] == desc.height &&
          header[6] == layerCount(desc.layout),
          "texture binary metadata disagrees with descriptor: {}",
          file.string());
    CHECK(std::filesystem::file_size(file) == headerBytes + header[7],
          "invalid texture binary size: {}",
          file.string());
    std::vector<uint8_t> bytes(header[7]);
    validate(desc, bytes, file);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    CHECK(input, "failed reading texture binary: {}", file.string());
    return bytes;
}
}
