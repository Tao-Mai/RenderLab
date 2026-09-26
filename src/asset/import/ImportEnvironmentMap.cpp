#include "asset/AssetImporter.h"

#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
#include "asset/import/ImportId.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/vec3.hpp>

namespace
{
[[nodiscard]] glm::vec3 faceDirection(uint32_t face, float u, float v)
{
    switch (face)
    {
        case 0:
            return glm::normalize(glm::vec3{1.0f, -v, -u});
        case 1:
            return glm::normalize(glm::vec3{-1.0f, -v, u});
        case 2:
            return glm::normalize(glm::vec3{u, 1.0f, v});
        case 3:
            return glm::normalize(glm::vec3{u, -1.0f, -v});
        case 4:
            return glm::normalize(glm::vec3{u, -v, 1.0f});
        default:
            return glm::normalize(glm::vec3{-u, -v, -1.0f});
    }
}

[[nodiscard]] std::array<float, 4> sampleEquirectangular(
    const float* image, uint32_t width, uint32_t height, const glm::vec3& direction)
{
    const float longitude = std::atan2(direction.x, direction.z);
    const float latitude  = std::acos(std::clamp(direction.y, -1.0f, 1.0f));
    const float x         = (0.5f - longitude / (2.0f * std::numbers::pi_v<float>)) *
        static_cast<float>(width - 1);
    const float y = std::clamp(latitude / std::numbers::pi_v<float> *
                               static_cast<float>(height - 1),
                               0.0f,
                               static_cast<float>(height - 1));
    const int   x0 = static_cast<int>(std::floor(x));
    const int   y0 = std::clamp(static_cast<int>(std::floor(y)), 0, static_cast<int>(height) - 1);
    const int   x1 = x0 + 1;
    const int   y1 = std::clamp(y0 + 1, 0, static_cast<int>(height) - 1);
    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);
    const auto  wrapX = [width](int value)
    {
        const int size = static_cast<int>(width);
        return static_cast<uint32_t>((value % size + size) % size);
    };
    const auto texel = [image, width](uint32_t px, int py, uint32_t channel)
    {
        return image[(static_cast<size_t>(py) * width + px) * 4 + channel];
    };

    std::array<float, 4> result{};
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        const float upper = std::lerp(texel(wrapX(x0), y0, channel),
                                      texel(wrapX(x1), y0, channel),
                                      tx);
        const float lower = std::lerp(texel(wrapX(x0), y1, channel),
                                      texel(wrapX(x1), y1, channel),
                                      tx);
        result[channel] = std::lerp(upper, lower, ty);
    }
    return result;
}

[[nodiscard]] std::array<float, 4> sampleOpenExrCube(
    const float* image, uint32_t side, uint32_t face, uint32_t x, uint32_t y)
{
    // OpenEXR stacks +X, -X, +Y, -Y, +Z, -Z vertically.
    const bool     flipVertical = face == 2 || face == 3;
    const uint32_t sourceX      = flipVertical ? x : side - 1 - x;
    const uint32_t sourceY      = flipVertical ? side - 1 - y : y;
    const float*   source       = image +
        ((static_cast<size_t>(face) * side + sourceY) * side + sourceX) * 4;
    return {source[0], source[1], source[2], source[3]};
}

// 三通道转为四通道
[[nodiscard]] ImageFormat environmentFormat(ImageFormat format)
{
    switch (format)
    {
        case ImageFormat::RGB8:
        case ImageFormat::RGBA8:
            return ImageFormat::RGBA8;
        case ImageFormat::RGBA16F:
            return ImageFormat::RGBA16F;
        case ImageFormat::RGBA32F:
            return ImageFormat::RGBA32F;
        default:
            LOG_FATAL("unsupported environment image format");
    }
}

void writePixel(uint8_t* output, const std::array<float, 4>& rgba, ImageFormat format)
{
    if (format == ImageFormat::RGBA8)
    {
        for (size_t channel = 0; channel < 4; ++channel)
        {
            const float linear  = std::clamp(rgba[channel], 0.0f, 1.0f);
            const float encoded = channel == 3
                ? linear
                : linear <= 0.0031308f
                ? linear * 12.92f
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            output[channel] = static_cast<uint8_t>(std::lround(encoded * 255.0f));
        }
    }
    else if (format == ImageFormat::RGBA16F)
    {
        const std::array<uint16_t, 4> half{
            glm::packHalf1x16(rgba[0]),
            glm::packHalf1x16(rgba[1]),
            glm::packHalf1x16(rgba[2]),
            glm::packHalf1x16(rgba[3]),
        };
        std::memcpy(output, half.data(), sizeof(half));
    }
    else
    {
        CHECK(format == ImageFormat::RGBA32F, "invalid EXR data format");
        std::memcpy(output, rgba.data(), sizeof(rgba));
    }
}

[[nodiscard]] std::vector<float> linearize8BitPixels(
    std::span<const uint8_t> pixels, uint32_t width, uint32_t height,
    ImageFormat              format)
{
    const size_t       channels   = AssetDataManager::bytesPerPixel(format);
    const size_t       pixelCount = static_cast<size_t>(width) * height;
    std::vector<float> linearPixels(pixelCount * 4);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        for (size_t channel = 0; channel < 3; ++channel)
        {
            const float encoded = static_cast<float>(
                pixels[pixel * channels + channel]) / 255.0f;
            linearPixels[pixel * 4 + channel] = encoded <= 0.04045f
                ? encoded / 12.92f
                : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }
        linearPixels[pixel * 4 + 3] = channels == 4
            ? static_cast<float>(pixels[pixel * channels + 3]) / 255.0f
            : 1.0f;
    }
    return linearPixels;
}
}

AssetImporter::LoadedImage::Projection AssetImporter::environmentProjection(
    const LoadedImage& image, const std::filesystem::path& source)
{
    const bool isLatLongShape = static_cast<uint64_t>(image.width) ==
        static_cast<uint64_t>(image.height) * 2;
    const bool isCubeShape = static_cast<uint64_t>(image.height) ==
        static_cast<uint64_t>(image.width) * 6;
    CHECK(image.projection.has_value() || isLatLongShape || isCubeShape,
          "environment image '{}' has no envmap attribute and is neither 2:1 nor 1:6",
          source.string());
    const LoadedImage::Projection projection = image.projection.value_or(
        isCubeShape ? LoadedImage::Projection::Cube : LoadedImage::Projection::LatLong);
    CHECK((projection == LoadedImage::Projection::LatLong && isLatLongShape) ||
          (projection == LoadedImage::Projection::Cube && isCubeShape),
          "environment image '{}' dimensions disagree with its envmap layout",
          source.string());
    CHECK(image.fileFormat == LoadedImage::FileFormat::Exr ||
          projection == LoadedImage::Projection::LatLong,
          "non-EXR environment images must use a 2:1 latitude-longitude layout: {}",
          source.string());
    CHECK(image.fileFormat == LoadedImage::FileFormat::Exr ||
          image.format == ImageFormat::RGB8 || image.format == ImageFormat::RGBA8,
          "8-bit environment images require RGB or RGBA channels: {}",
          source.string());
    return projection;
}

std::vector<uint8_t> AssetImporter::environmentCubemap(
    const LoadedImage& image, LoadedImage::Projection       projection, uint32_t side,
    ImageFormat        format, const std::filesystem::path& source)
{
    const uint64_t byteCount = static_cast<uint64_t>(side) * side * 6 *
        AssetDataManager::bytesPerPixel(format);
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
          "imported cubemap is too large: {}",
          source.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
    const size_t         bytesPerPixel = AssetDataManager::bytesPerPixel(format);
    std::vector<float>   linearPixels;
    const float*         decoded = nullptr;
    if (const auto* floats = std::get_if<std::vector<float>>(&image.pixels))
    {
        decoded = floats->data();
    }
    else
    {
        linearPixels = linearize8BitPixels(
            std::get<std::vector<uint8_t>>(image.pixels),
            image.width,
            image.height,
            image.format);
        decoded = linearPixels.data();
    }
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < side; ++y)
        {
            for (uint32_t x = 0; x < side; ++x)
            {
                const float u    = 2.0f * (static_cast<float>(x) + 0.5f) / side - 1.0f;
                const float v    = 2.0f * (static_cast<float>(y) + 0.5f) / side - 1.0f;
                const auto  rgba = projection == LoadedImage::Projection::Cube
                    ? sampleOpenExrCube(decoded, side, face, x, y)
                    : sampleEquirectangular(decoded,
                                            image.width,
                                            image.height,
                                            faceDirection(face, u, v));
                uint8_t* output = bytes.data() +
                    ((static_cast<size_t>(face) * side + y) * side + x) * bytesPerPixel;
                writePixel(output, rgba, format);
            }
        }
    }
    return bytes;
}

AssetId AssetImporter::importEnvironmentMap(const std::filesystem::path& source)
{
    const LoadedImage             image      = loadImage(source);
    const LoadedImage::Projection projection = environmentProjection(image, source);
    const uint32_t                side       = projection == LoadedImage::Projection::Cube
        ? image.width
        : std::max(1u, image.height / 2);

    TextureDesc radiance{};
    radiance.id                      = asset_import::nextId<TextureDesc>(source);
    radiance.source                  = Source::File;
    radiance.format                  = environmentFormat(image.format);
    radiance.colorSpace              = defaultColorSpace(image.fileFormat);
    radiance.layout                  = ImageLayout::Cubemap;
    radiance.width                   = side;
    radiance.height                  = side;
    const std::vector<uint8_t> bytes = environmentCubemap(
        image,
        projection,
        side,
        radiance.format,
        source);
    const AssetId radianceId = saveTexture(std::move(radiance), source, bytes);

    EnvironmentMapDesc environment{};
    environment.id       = asset_import::nextId<EnvironmentMapDesc>(source);
    environment.radiance = radianceId;
    return context().assetDescManager->save(std::move(environment));
}
