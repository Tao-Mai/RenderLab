#include "asset/texture_io.h"

#include "core/logger.h"

#include <array>
#include <fstream>
#include <limits>

namespace
{
constexpr uint32_t textureMagic = 0x58455452u; // RTEX
constexpr uint32_t textureVersion = 1;
constexpr uint64_t headerBytes = 8 * sizeof(uint32_t);

[[nodiscard]] uint32_t layerCount(TextureDesc::Layout layout)
{
    switch (layout)
    {
    case TextureDesc::Layout::Image2D: return 1;
    case TextureDesc::Layout::Cubemap: return 6;
    }
    LOG_FATAL("invalid texture layout");
}

void validate(const TexturePixels& pixels, const std::filesystem::path& file)
{
    CHECK(pixels.width > 0 && pixels.height > 0,
        "invalid texture dimensions: {}", file.string());
    CHECK(pixels.layout != TextureDesc::Layout::Cubemap ||
            pixels.width == pixels.height,
        "cubemap faces must be square: {}", file.string());
    const uint64_t maxPayload = std::numeric_limits<uint32_t>::max();
    CHECK(pixels.width <= maxPayload / pixels.height /
            layerCount(pixels.layout) / texture_io::bytesPerPixel(pixels.format),
        "texture payload is too large: {}", file.string());
    const uint64_t expected = static_cast<uint64_t>(pixels.width) * pixels.height *
        layerCount(pixels.layout) * texture_io::bytesPerPixel(pixels.format);
    CHECK(expected == pixels.bytes.size(),
        "invalid texture payload size: {}", file.string());
}
}

namespace texture_io
{
uint32_t bytesPerPixel(TextureDesc::DataFormat format)
{
    switch (format)
    {
    case TextureDesc::DataFormat::Rgba8Srgb: return 4;
    case TextureDesc::DataFormat::Rgba16Float: return 8;
    case TextureDesc::DataFormat::Rgba32Float: return 16;
    }
    LOG_FATAL("invalid texture data format");
}

void write(const std::filesystem::path& file, const TexturePixels& pixels)
{
    validate(pixels, file);
    std::filesystem::create_directories(file.parent_path());
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    CHECK(output, "cannot write texture binary: {}", temporary.string());
    const std::array<uint32_t, 8> header{
        textureMagic, textureVersion,
        static_cast<uint32_t>(pixels.layout),
        static_cast<uint32_t>(pixels.format),
        pixels.width, pixels.height, layerCount(pixels.layout),
        static_cast<uint32_t>(pixels.bytes.size()),
    };
    output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    output.write(reinterpret_cast<const char*>(pixels.bytes.data()),
        static_cast<std::streamsize>(pixels.bytes.size()));
    output.close();
    CHECK(output, "failed writing texture binary: {}", temporary.string());
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    CHECK(!error, "failed replacing texture binary '{}': {}",
        file.string(), error.message());
}

TexturePixels read(const std::filesystem::path& file)
{
    std::ifstream input(file, std::ios::binary);
    CHECK(input, "cannot open texture binary: {}", file.string());
    std::array<uint32_t, 8> header{};
    input.read(reinterpret_cast<char*>(header.data()), sizeof(header));
    CHECK(input && header[0] == textureMagic && header[1] == textureVersion,
        "invalid texture binary header: {}", file.string());
    CHECK(header[2] <= static_cast<uint32_t>(TextureDesc::Layout::Cubemap) &&
            header[3] <= static_cast<uint32_t>(TextureDesc::DataFormat::Rgba32Float),
        "invalid texture binary format: {}", file.string());
    TexturePixels pixels{
        .format = static_cast<TextureDesc::DataFormat>(header[3]),
        .layout = static_cast<TextureDesc::Layout>(header[2]),
        .width = header[4],
        .height = header[5],
    };
    CHECK(header[6] == layerCount(pixels.layout) &&
            std::filesystem::file_size(file) == headerBytes + header[7],
        "invalid texture binary size: {}", file.string());
    pixels.bytes.resize(header[7]);
    validate(pixels, file);
    input.read(reinterpret_cast<char*>(pixels.bytes.data()),
        static_cast<std::streamsize>(pixels.bytes.size()));
    CHECK(input, "failed reading texture binary: {}", file.string());
    return pixels;
}
}
