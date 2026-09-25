#pragma once

#include "asset/AssetId.h"
#include "asset/ImageFormat.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <variant>
#include <vector>

struct TextureDesc;

class AssetImporter
{
public:
    static AssetId importTexture(
        const std::filesystem::path& source,
        std::optional<ColorSpace> colorSpace = std::nullopt);

    static AssetId importEnvironmentMap(const std::filesystem::path& source);

    static AssetId importMesh(const std::filesystem::path& source);

private:
    struct LoadedImage
    {
        enum class Projection
        {
            LatLong,
            Cube,
        };

        enum class FileFormat
        {
            Exr,
            JPEG, // Other 8-bit formats decoded by stb use the same color-space default.
        };

        ImageFormat                                            format;
        FileFormat                                             fileFormat;
        uint32_t                                               width  = 0;
        uint32_t                                               height = 0;
        std::optional<Projection>                              projection;
        std::variant<std::vector<uint8_t>, std::vector<float>> pixels;
    };

    [[nodiscard]] static LoadedImage loadImage(const std::filesystem::path& source);
    [[nodiscard]] static ColorSpace defaultColorSpace(LoadedImage::FileFormat format);
    [[nodiscard]] static LoadedImage::Projection environmentProjection(
        const LoadedImage& image, const std::filesystem::path& source);
    [[nodiscard]] static std::vector<uint8_t> environmentCubemap(
        const LoadedImage& image,
        LoadedImage::Projection projection,
        uint32_t side,
        ImageFormat format,
        const std::filesystem::path& source);
    [[nodiscard]] static AssetId     saveTexture(
        TextureDesc                  desc,
        const std::filesystem::path& source,
        std::span<const uint8_t>     bytes);
};
