#include "asset/AssetImporter.h"

#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/import/EnvironmentMapUtils.h"
#include "asset/import/ImportId.h"
#include "asset/TextureMip.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
using asset_import::environment_map::decode8BitPixels;
using asset_import::environment_map::environmentFormat;
using asset_import::environment_map::faceDirection;
using asset_import::environment_map::sampleEquirectangular;
using asset_import::environment_map::sampleOpenExrCube;
using asset_import::environment_map::writePixel;

[[nodiscard]] double elapsedSeconds(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

[[nodiscard]] TextureAsset cubemapAsset(
    const std::filesystem::path& source,
    ImageFormat format, ColorSpace colorSpace, uint32_t side, uint32_t mipLevels)
{
    return {
        .id = TextureAsset::ID{source.stem().string()},
        .source = Source::File,
        .format = format,
        .colorSpace = colorSpace,
        .layout = ImageLayout::Cubemap,
        .width = side,
        .height = side,
        .mipLevels = mipLevels,
    };
}
}


// 输入：image 含源文件格式、宽高和可选 EXR envmap 投影标记，source 用于报错。
// 输出：LatLong 或 Cube；缺少标记时按 2:1 或 1:6 尺寸推断。LatLong 是
// width x height 的单张经纬图，Cube 是 width x (6 * width) 的竖排六面图。
// 同时拒绝尺寸与投影不符的文件，以及非 EXR 的竖排 Cube。
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
    CHECK(image.fileFormat != LoadedImage::FileFormat::JPEG ||
          image.format == ImageFormat::RGB8 || image.format == ImageFormat::RGBA8,
          "8-bit environment images require RGB or RGBA channels: {}",
          source.string());

    return projection;
}

// 输入：image.pixels 为行优先源图；EXR/HDR 是每像素交错 RGBA 的 float[4]，
// 8 位图像是交错 RGB8/RGBA8。projection 决定按经纬图重采样或从竖排 EXR
// 取面；side 是目标面的宽高，format 和 colorSpace 是四通道输出格式与色彩空间，
// source 用于报错。8 位输入依 colorSpace 转为线性值后采样。
// 输出：线性 RGBA float 和待保存的字节数据，共用 +X、-X、+Y、-Y、+Z、-Z 面顺序。
// 每面 side x side、行优先、每像素 RGBA 交错；线性数据按 4 个 float 寻址，
// 字节数据按像素大小寻址，均没有行间距或面间距。
// RGBA8 字节按 colorSpace 编码，浮点格式的字节保持线性值。
AssetImporter::CubemapPixels AssetImporter::environmentCubemap(
    const LoadedImage& image, LoadedImage::Projection projection, uint32_t                     side,
    ImageFormat        format, ColorSpace             colorSpace, const std::filesystem::path& source)
{
    const size_t   bytesPerPixel = imageBlock(format).bytes;
    const uint64_t byteCount     = static_cast<uint64_t>(side) * side * 6 * bytesPerPixel;
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
          "imported cubemap is too large: {}",
          source.string());

    CubemapPixels cubemap;
    cubemap.encoded.resize(byteCount);
    cubemap.linear.resize(static_cast<size_t>(side) * side * 6 * 4);

    std::vector<float> linearPixels;
    const float*       decoded = nullptr;

    if (const auto* floats = std::get_if<std::vector<float>>(&image.pixels))
    {
        decoded = floats->data();
    }
    else
    {

        linearPixels = decode8BitPixels(
            std::get<std::vector<uint8_t>>(image.pixels),
            image.width,
            image.height,
            image.format,
            colorSpace);
        decoded = linearPixels.data();
    }

    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < side; ++y)
        {
            for (uint32_t x = 0; x < side; ++x)
            {
                std::array<float, 4> rgba{};
                if (projection == LoadedImage::Projection::Cube)
                {
                    rgba = sampleOpenExrCube(decoded, side, face, x, y);
                }
                else
                {
                    const float u = 2.0f * (static_cast<float>(x) + 0.5f) / side - 1.0f;
                    const float v = 2.0f * (static_cast<float>(y) + 0.5f) / side - 1.0f;
                    rgba          = sampleEquirectangular(decoded,
                                                 image.width,
                                                 image.height,
                                                 faceDirection(face, u, v));
                }

                const size_t pixel = (static_cast<size_t>(face) * side + y) * side + x;
                std::copy(rgba.begin(), rgba.end(), cubemap.linear.begin() + pixel * 4);

                uint8_t* output = cubemap.encoded.data() + pixel * bytesPerPixel;
                writePixel(output, rgba, format, colorSpace);
            }
        }
    }

    return cubemap;
}

std::optional<AssetImporter::PreparedEnvironmentMap> AssetImporter::prepareEnvironmentMap(
    const ImportEnvironmentMapSetting& setting, std::stop_token stop)
{
    CHECK(!setting.source.empty(), "environment map source path is empty");
    if (stop.stop_requested())
    {
        return std::nullopt;
    }

    const std::filesystem::path& source = setting.source;
    const auto importStart = std::chrono::steady_clock::now();
    LOG_INFO("Decoding environment source '{}', this may take time for large EXR files",
             source.string());
    const LoadedImage             image      = loadImage(source);
    if (stop.stop_requested())
    {
        return std::nullopt;
    }

    const LoadedImage::Projection projection = environmentProjection(image, source);
    const uint32_t                side       = projection == LoadedImage::Projection::Cube
        ? image.width
        : std::max(1u, image.height / 2);
    const ColorSpace resolvedColorSpace = setting.colorSpace.value_or(
        defaultColorSpace(image.fileFormat));
    LOG_INFO("Decoded environment source: {}x{}, cubemap face {}x{}, elapsed {:.1f}s",
             image.width, image.height, side, side, elapsedSeconds(importStart));

    TextureAsset radiance = cubemapAsset(
        source, environmentFormat(image.format), resolvedColorSpace,
        side, texture_mip::maxLevels(side, side));
    CHECK(radiance.format == ImageFormat::RGBA8 ||
          radiance.colorSpace == ColorSpace::Linear,
          "floating-point environment textures require linear color space: {}",
          source.string());
    LOG_INFO("Converting environment to radiance cubemap ({} mips)",
             radiance.mipLevels);
    CubemapPixels radiancePixels = environmentCubemap(
        image,
        projection,
        side,
        radiance.format,
        radiance.colorSpace,
        source);
    if (stop.stop_requested())
    {
        return std::nullopt;
    }

    LOG_INFO("Radiance base cubemap ready, elapsed {:.1f}s",
             elapsedSeconds(importStart));
    buildMipmapChain(radiancePixels, radiance, stop);
    if (stop.stop_requested())
    {
        return std::nullopt;
    }

    LOG_INFO("Radiance mip chain ready, elapsed {:.1f}s",
             elapsedSeconds(importStart));

    return PreparedEnvironmentMap{
        .source = source,
        .radiance = std::move(radiance),
        .radianceBytes = std::move(radiancePixels.encoded),
    };
}

EnvironmentMapAsset::ID AssetImporter::saveEnvironmentMap(PreparedEnvironmentMap&& prepared)
{
    const std::filesystem::path& source = prepared.source;
    prepared.radiance.id = asset_import::nextId<TextureAsset>(source.stem().string());
    const auto radianceId = saveTexture(std::move(prepared.radiance), source, prepared.radianceBytes);
    EnvironmentMapAsset environment;
    environment.id = asset_import::nextId<EnvironmentMapAsset>(source.stem().string());
    environment.radiance = {radianceId, BuiltinAssets::Sampler::linearClamp};

    const EnvironmentMapAsset::ID id =
        context().assetManager->save<EnvironmentMapAsset>(std::move(environment));
    LOG_INFO("Import environment '{}' saved as '{}'", source.string(), id.value);
    return id;
}

EnvironmentMapAsset::ID AssetImporter::importEnvironmentMap(const ImportEnvironmentMapSetting& setting)
{
    auto prepared = prepareEnvironmentMap(setting);
    CHECK(prepared.has_value(), "environment import was canceled: {}", setting.source.string());
    return saveEnvironmentMap(std::move(*prepared));
}
