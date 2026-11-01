#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "asset/AssetImporter.h"

#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tinyexr.h>

AssetImporter::LoadedImage readPbrtPfm(const std::filesystem::path& path);

namespace
{
struct StbiDeleter
{
    void operator()(void* pixels) const { stbi_image_free(pixels); }
};

struct ExrPixelsDeleter
{
    void operator()(float* pixels) const { std::free(pixels); }
};

struct ExrHeaderDeleter
{
    void operator()(EXRHeader* header) const { FreeEXRHeader(header); }
};

struct ExrErrorDeleter
{
    void operator()(const char* message) const { FreeEXRErrorMessage(message); }
};

[[nodiscard]] bool isColorChannel(const char* name)
{
    const std::string_view channel{name};
    return channel == "R" || channel == "G" || channel == "B" ||
        channel.ends_with(".R") || channel.ends_with(".G") ||
        channel.ends_with(".B");
}
}

AssetImporter::LoadedImage AssetImporter::loadImage(const std::filesystem::path& source)
{
    std::string extension = source.extension().string();
    std::ranges::transform(extension,
                           extension.begin(),
                           [](unsigned char value)
                           {
                               return static_cast<char>(std::tolower(value));
                           });
    const std::string filename = source.string();
    if (extension == ".pfm") return readPbrtPfm(source);

    if (extension == ".exr")
    {
        LOG_INFO("Reading EXR header: '{}'", filename);
        EXRVersion version{};
        CHECK(ParseEXRVersionFromFile(&version, filename.c_str()) == TINYEXR_SUCCESS &&
              !version.multipart && !version.non_image,
              "unsupported EXR version or multipart image: {}",
              filename);

        EXRHeader header{};
        InitEXRHeader(&header);
        std::unique_ptr<EXRHeader, ExrHeaderDeleter> headerGuard(&header);
        const char*                                  headerError  = nullptr;
        const int                                    headerResult = ParseEXRHeaderFromFile(
            &header,
            &version,
            filename.c_str(),
            &headerError);
        std::unique_ptr<const char, ExrErrorDeleter> headerErrorGuard(headerError);
        CHECK(headerResult == TINYEXR_SUCCESS,
              "failed to read EXR header '{}': {}",
              filename,
              headerError ? headerError : "unknown error");

        int  colorChannels = 0;
        bool hasFloat      = false;
        for (int index = 0; index < header.num_channels; ++index)
        {
            const EXRChannelInfo& channel = header.channels[index];
            if (!isColorChannel(channel.name) && header.num_channels != 1)
            {
                continue;
            }
            CHECK(channel.pixel_type == TINYEXR_PIXELTYPE_HALF ||
                  channel.pixel_type == TINYEXR_PIXELTYPE_FLOAT,
                  "unsupported EXR color channel type in '{}'",
                  filename);
            hasFloat |= channel.pixel_type == TINYEXR_PIXELTYPE_FLOAT;
            ++colorChannels;
        }
        CHECK(colorChannels >= 3 || header.num_channels == 1, "EXR has no RGB or scalar channel: {}", filename);
        LOG_INFO("EXR header ready: {} color channels, {} precision",
                 colorChannels, hasFloat ? "float" : "half");

        std::optional<LoadedImage::Projection> projection;
        std::optional<RgbChromaticities> chromaticities;
        for (int index = 0; index < header.num_custom_attributes; ++index)
        {
            const EXRAttribute& attribute = header.custom_attributes[index];
            if (std::string_view{attribute.name} == "chromaticities")
            {
                CHECK(std::string_view{attribute.type} == "chromaticities" && attribute.size == 8 * sizeof(float),
                      "invalid EXR chromaticities: {}", filename);
                std::array<float, 8> values;
                std::memcpy(values.data(), attribute.value, sizeof(values));
                chromaticities = RgbChromaticities{{values[0], values[1]}, {values[2], values[3]},
                    {values[4], values[5]}, {values[6], values[7]}};
                continue;
            }
            if (std::string_view{attribute.name} != "envmap")
            {
                continue;
            }
            CHECK(std::string_view{attribute.type} == "envmap" &&
                  attribute.size == 1 && attribute.value != nullptr,
                  "invalid EXR envmap attribute: {}",
                  filename);
            if (attribute.value[0] == 0)
            {
                projection = LoadedImage::Projection::LatLong;
            }
            else
            {
                CHECK(attribute.value[0] == 1,
                      "unknown EXR envmap layout in '{}'",
                      filename);
                projection = LoadedImage::Projection::Cube;
            }
        }

        float* raw = nullptr;
        int width = 0;
        int height = 0;
        const char* decodeError = nullptr;
        LOG_INFO("Decoding EXR pixels: '{}'", filename);
        const int result = LoadEXR(&raw, &width, &height, filename.c_str(), &decodeError);
        std::unique_ptr<float, ExrPixelsDeleter> decoded(raw);
        std::unique_ptr<const char, ExrErrorDeleter> decodeErrorGuard(decodeError);
        CHECK(result == TINYEXR_SUCCESS && decoded && width > 0 && height > 0,
              "failed to decode EXR '{}': {}",
              filename,
              decodeError ? decodeError : "unknown error");
        const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
        CHECK(pixelCount <= std::numeric_limits<size_t>::max() / 4,
              "EXR image is too large: {}",
              filename);
        std::vector<float> pixels(decoded.get(), decoded.get() + pixelCount * 4);
        return {
            .format = hasFloat ? ImageFormat::RGBA32F : ImageFormat::RGBA16F,
            .fileFormat = LoadedImage::FileFormat::Exr,
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height),
            .projection = projection,
            .chromaticities = chromaticities,
            .pixels = std::move(pixels),
        };
    }

    if (extension == ".hdr")
    {
        LOG_INFO("Decoding HDR pixels: '{}'", filename);
        int width = 0;
        int height = 0;
        int channels = 0;
        std::unique_ptr<float, StbiDeleter> decoded(
            stbi_loadf(filename.c_str(), &width, &height, &channels, 4));
        CHECK(decoded && width > 0 && height > 0 && channels >= 1 && channels <= 4,
              "failed to load HDR image '{}': {}",
              filename,
              stbi_failure_reason());

        const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
        CHECK(pixelCount <= std::numeric_limits<size_t>::max() / 4,
              "HDR image is too large: {}",
              filename);
        std::vector<float> pixels(decoded.get(), decoded.get() + pixelCount * 4);
        return {
            .format = ImageFormat::RGBA32F,
            .fileFormat = LoadedImage::FileFormat::Hdr,
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height),
            .pixels = std::move(pixels),
        };
    }

    CHECK(extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
          extension == ".bmp" || extension == ".tga" || extension == ".gif" ||
          extension == ".psd" || extension == ".pic" || extension == ".pnm",
          "unsupported image format: {}",
          filename);
    if (stbi_is_16_bit(filename.c_str()))
    {
        int width = 0, height = 0, channels = 0;
        std::unique_ptr<stbi_us, StbiDeleter> decoded(stbi_load_16(filename.c_str(), &width, &height, &channels, 4));
        CHECK(decoded && width > 0 && height > 0, "failed to read 16-bit image '{}': {}", filename, stbi_failure_reason());
        const size_t count = size_t(width) * height * 4;
        std::vector<float> pixels(count);
        constexpr float kMaxInteger16 = 65535.0f;
        for (size_t index = 0; index < count; ++index)
            pixels[index] = decoded.get()[index] / kMaxInteger16;
        return {.format = ImageFormat::RGBA32F, .fileFormat = LoadedImage::FileFormat::Integer16,
            .width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height), .pixels = std::move(pixels)};
    }
    int                                   width    = 0;
    int                                   height   = 0;
    int                                   channels = 0;
    std::unique_ptr<stbi_uc, StbiDeleter> decoded(
        stbi_load(filename.c_str(), &width, &height, &channels, 0));
    CHECK(decoded && width > 0 && height > 0 && channels >= 1 && channels <= 4,
          "failed to load image '{}': {}",
          filename,
          stbi_failure_reason());
    const uint64_t byteCount = static_cast<uint64_t>(width) * height * channels;
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
          "image is too large: {}",
          filename);
    std::vector<uint8_t> pixels(decoded.get(), decoded.get() + byteCount);
    return {
        .format = channels == 1
        ? ImageFormat::R8
        : channels == 2
        ? ImageFormat::RG8
        : channels == 3
        ? ImageFormat::RGB8
        : ImageFormat::RGBA8,
        .fileFormat = LoadedImage::FileFormat::JPEG,
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .pixels = std::move(pixels),
    };
}

// 下层只读数据，由上层判断解释方法
ColorSpace AssetImporter::defaultColorSpace(LoadedImage::FileFormat format)
{
    switch (format)
    {
        case LoadedImage::FileFormat::Exr:
        case LoadedImage::FileFormat::Hdr:
        case LoadedImage::FileFormat::Pfm:
            return ColorSpace::Linear;
        case LoadedImage::FileFormat::JPEG:
        case LoadedImage::FileFormat::Integer16:
            return ColorSpace::Srgb;
    }
    LOG_FATAL("invalid image file format");
}
