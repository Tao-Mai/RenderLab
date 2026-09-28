#pragma once

#include "asset/AssetDesc.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <variant>
#include <vector>

struct ImportEnvironmentMapSetting
{
    // 源图与 radiance 的色彩空间；未指定时按文件格式推断。
    std::optional<ColorSpace> colorSpace;
    // Irradiance Cubemap 每个面的宽高；输出始终是线性 RGBA32F。
    uint32_t irradianceSize = 32;
    // 每个输出方向的余弦加权半球采样数。
    uint32_t irradianceSampleCount = 2048;
    // GGX 预滤波每个输出方向的半程向量采样数。
    uint32_t prefilteredSpecularSampleCount = 128;
};

class AssetImporter
{
public:
    static Texture::ID importTexture(
        const std::filesystem::path& source,
        std::optional<ColorSpace> colorSpace = std::nullopt);

    static EnvironmentMap::ID importEnvironmentMap(
        const std::filesystem::path& source,
        const ImportEnvironmentMapSetting& setting = {});

    static Mesh::ID importMesh(const std::filesystem::path& source);

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

    struct CubemapPixels
    {
        std::vector<float> linear;
        std::vector<uint8_t> encoded;
    };

    [[nodiscard]] static LoadedImage loadImage(const std::filesystem::path& source);
    [[nodiscard]] static ColorSpace defaultColorSpace(LoadedImage::FileFormat format);
    [[nodiscard]] static LoadedImage::Projection environmentProjection(
        const LoadedImage& image, const std::filesystem::path& source);
    [[nodiscard]] static CubemapPixels environmentCubemap(
        const LoadedImage& image,
        LoadedImage::Projection projection,
        uint32_t side,
        ImageFormat format,
        ColorSpace colorSpace,
        const std::filesystem::path& source);
    [[nodiscard]] static std::vector<uint8_t> irradianceCubemap(
        const Texture::Desc& radiance,
        std::span<const float> radiancePixels,
        uint32_t side,
        uint32_t sampleCount);
    [[nodiscard]] static std::vector<uint8_t> prefilterSpecularMap(
        const Texture::Desc& radiance,
        std::span<const float> radiancePixels,
        uint32_t sampleCount);
    [[nodiscard]] static Texture::ID saveTexture(
        Texture::Desc                  desc,
        const std::filesystem::path& source,
        std::span<const uint8_t>     bytes);
};
