#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "asset/AssetImporter.h"

#include "core/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tinyexr.h>

namespace
{
struct StbiDeleter
{
    void operator()(stbi_uc* pixels) const { stbi_image_free(pixels); }
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

    if (extension == ".exr")
    {
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
            if (!isColorChannel(channel.name))
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
        CHECK(colorChannels >= 3, "EXR has no RGB channels: {}", filename);

        std::optional<LoadedImage::Projection> projection;
        for (int index = 0; index < header.num_custom_attributes; ++index)
        {
            const EXRAttribute& attribute = header.custom_attributes[index];
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
            break;
        }

        float* raw = nullptr;
        int width = 0;
        int height = 0;
        const char* decodeError = nullptr;
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
            .pixels = std::move(pixels),
        };
    }

    CHECK(extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
          extension == ".bmp" || extension == ".tga" || extension == ".gif" ||
          extension == ".psd" || extension == ".pic" || extension == ".pnm",
          "unsupported image format: {}",
          filename);
    CHECK(!stbi_is_16_bit(filename.c_str()),
          "16-bit integer images are not supported: {}",
          filename);
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
        .format = channels == 1 ? ImageFormat::R8 :
                  channels == 2 ? ImageFormat::RG8 :
                  channels == 3 ? ImageFormat::RGB8 : ImageFormat::RGBA8,
        .fileFormat = LoadedImage::FileFormat::JPEG,
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .pixels = std::move(pixels),
    };
}

ColorSpace AssetImporter::defaultColorSpace(LoadedImage::FileFormat format)
{
    switch (format)
    {
        case LoadedImage::FileFormat::Exr: return ColorSpace::Linear;
        case LoadedImage::FileFormat::JPEG: return ColorSpace::Srgb;
    }
    LOG_FATAL("invalid image file format");
}
