#include "asset/AssetImporter.h"

#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
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
#include <string_view>
#include <utility>
#include <vector>

// 执行流程：loadImage 解码源文件 -> environmentProjection 验证经纬图或竖排 Cube ->
// environmentCubemap 生成 radiance 六面图 -> buildMipmapChain 生成 radiance mip 链 ->
// irradianceCubemap 做余弦加权半球积分 ->
// prefilterSpecularMap 生成 GGX 预滤波 mip 链 -> 保存纹理及 EnvironmentMap::Desc。
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

[[nodiscard]] uint64_t estimatedPrefilterSamples(
    uint32_t side, uint32_t mipLevels, uint32_t firstSamples, uint32_t maxSamples)
{
    uint64_t total = 0;
    uint32_t samples = firstSamples;
    for (uint32_t mip = 1; mip < mipLevels; ++mip)
    {
        const uint64_t mipSide = texture_mip::extent(side, mip);
        total += 6 * mipSide * mipSide * samples;
        samples = static_cast<uint32_t>(std::min<uint64_t>(
            static_cast<uint64_t>(samples) * 2, maxSamples));
    }
    return total;
}

void validateSetting(const ImportEnvironmentMapSetting& setting)
{
    const auto& source = setting.source;
    CHECK(!source.empty(), "environment map source path is empty");
    CHECK(setting.irradianceSize > 0,
          "irradiance map size must be positive: {}", source.string());
    CHECK(setting.irradianceSampleCount > 0,
          "irradiance sample count must be positive: {}", source.string());
    CHECK(setting.prefilteredSpecularSampleCount > 0,
          "prefilter sample count must be positive: {}", source.string());
    CHECK(setting.prefilteredSpecularMaxSampleCount >=
          setting.prefilteredSpecularSampleCount,
          "prefilter sample cap must be at least the initial sample count: {}",
          source.string());

    constexpr uint64_t pixelBytes = 4 * sizeof(float);
    constexpr uint64_t maxPayload = std::numeric_limits<uint32_t>::max();
    CHECK(setting.irradianceSize <=
          maxPayload / setting.irradianceSize / 6 / pixelBytes,
          "irradiance map is too large: {}", source.string());
}

[[nodiscard]] std::filesystem::path sourceWithSuffix(
    const std::filesystem::path& source, std::string_view suffix)
{
    return suffix.empty()
        ? source
        : source.parent_path() /
            (source.stem().string() + std::string{suffix} + source.extension().string());
}

[[nodiscard]] Texture::Desc cubemapDesc(
    const std::filesystem::path& source, std::string_view suffix,
    ImageFormat format, ColorSpace colorSpace, uint32_t side, uint32_t mipLevels)
{
    const std::filesystem::path name = sourceWithSuffix(source, suffix);
    return {
        .id = Texture::ID{name.stem().string()},
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
// 字节数据按 bytesPerPixel(format) 寻址，均没有行间距或面间距。
// RGBA8 字节按 colorSpace 编码，浮点格式的字节保持线性值。
AssetImporter::CubemapPixels AssetImporter::environmentCubemap(
    const LoadedImage& image, LoadedImage::Projection projection, uint32_t                     side,
    ImageFormat        format, ColorSpace             colorSpace, const std::filesystem::path& source)
{
    const size_t   bytesPerPixel = AssetDataManager::bytesPerPixel(format);
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

// 输入：setting.source 是 EXR、HDR 或支持的 8 位图像文件路径；setting 还可指定源图与 radiance
// 的色彩空间、irradiance 的面尺寸、两种积分的采样数及 GGX 采样上限。
// 省略色彩空间时 EXR 和 HDR 为 Linear、
// 8 位图像为 Srgb。loadImage 返回行优先源图。
// 输出：待写入的纹理描述与字节缓冲区。函数选定投影和面尺寸，将六面像素按
// environmentCubemap 所述布局写入 radiance 并生成完整 mip 链；再从 radiance 计算线性
// irradiance 纹理，并生成带完整 mip 链的 GGX 预滤波纹理。此阶段只计算，不写资产管理器。
std::optional<AssetImporter::PreparedEnvironmentMap> AssetImporter::prepareEnvironmentMap(
    const ImportEnvironmentMapSetting& setting, std::stop_token stop)
{
    validateSetting(setting);
    if (stop.stop_requested())
    {
        return std::nullopt;
    }

    const std::filesystem::path& source = setting.source;
    const auto importStart = std::chrono::steady_clock::now();
    LOG_INFO("Import environment '{}' started: irradiance {}x{}x6, {} samples; "
             "prefilter {} to {} samples per pixel",
             source.string(), setting.irradianceSize, setting.irradianceSize,
             setting.irradianceSampleCount,
             setting.prefilteredSpecularSampleCount,
             setting.prefilteredSpecularMaxSampleCount);

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

    Texture::Desc radiance = cubemapDesc(
        source, "", environmentFormat(image.format), resolvedColorSpace,
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

    Texture::Desc irradiance = cubemapDesc(
        source, "_irradiance", ImageFormat::RGBA32F, ColorSpace::Linear,
        setting.irradianceSize, 1);
    Texture::Desc prefiltered = cubemapDesc(
        source, "_prefiltered_specular", ImageFormat::RGBA32F, ColorSpace::Linear,
        side, radiance.mipLevels);

    LOG_INFO("Baking irradiance cubemap");
    std::vector<uint8_t> irradianceBytes =
        irradianceCubemap(radiance,
                          radiancePixels.linear,
                          setting.irradianceSize,
                          setting.irradianceSampleCount,
                          stop);
    if (stop.stop_requested())
    {
        return std::nullopt;
    }

    LOG_INFO("Irradiance cubemap ready, elapsed {:.1f}s",
             elapsedSeconds(importStart));

    const uint64_t estimatedSamples = estimatedPrefilterSamples(
        side, radiance.mipLevels,
        setting.prefilteredSpecularSampleCount,
        setting.prefilteredSpecularMaxSampleCount);
    LOG_INFO("Baking prefiltered specular cubemap: approximately {} candidate samples "
             "across {} mips", estimatedSamples, prefiltered.mipLevels);
    if (estimatedSamples > 100'000'000)
    {
        LOG_WARNING("Prefilter bake is large; it may take substantial CPU time");
    }
    std::vector<uint8_t> prefilteredBytes = prefilterSpecularMap(
        radiance,
        radiancePixels.linear,
        setting.prefilteredSpecularSampleCount,
        setting.prefilteredSpecularMaxSampleCount,
        stop);
    if (stop.stop_requested())
    {
        return std::nullopt;
    }

    LOG_INFO("Prefiltered specular cubemap ready, elapsed {:.1f}s",
             elapsedSeconds(importStart));

    return PreparedEnvironmentMap{
        .source = source,
        .radiance = std::move(radiance),
        .irradiance = std::move(irradiance),
        .prefiltered = std::move(prefiltered),
        .radianceBytes = std::move(radiancePixels.encoded),
        .irradianceBytes = std::move(irradianceBytes),
        .prefilteredBytes = std::move(prefilteredBytes),
    };
}

EnvironmentMap::ID AssetImporter::saveEnvironmentMap(PreparedEnvironmentMap&& prepared)
{
    const std::filesystem::path& source = prepared.source;
    prepared.radiance.id = asset_import::nextId<Texture>(source);
    prepared.irradiance.id = asset_import::nextId<Texture>(
        sourceWithSuffix(source, "_irradiance"));
    prepared.prefiltered.id = asset_import::nextId<Texture>(
        sourceWithSuffix(source, "_prefiltered_specular"));

    LOG_INFO("Writing environment textures: radiance {} bytes, irradiance {} bytes, "
             "prefiltered {} bytes",
             prepared.radianceBytes.size(), prepared.irradianceBytes.size(),
             prepared.prefilteredBytes.size());
    const Texture::ID radianceId =
        saveTexture(std::move(prepared.radiance), source, prepared.radianceBytes);
    const Texture::ID irradianceId =
        saveTexture(std::move(prepared.irradiance), source, prepared.irradianceBytes);
    const Texture::ID prefilteredId =
        saveTexture(std::move(prepared.prefiltered), source, prepared.prefilteredBytes);

    EnvironmentMap::Desc environment{};
    environment.id       = asset_import::nextId<EnvironmentMap>(source);
    environment.radiance = TextureBinding{
        radianceId, BuiltinAssets::Sampler::linearClamp};
    environment.irradiance = TextureBinding{
        irradianceId, BuiltinAssets::Sampler::linearClamp};
    environment.prefilteredSpecular = TextureBinding{
        prefilteredId, BuiltinAssets::Sampler::linearClamp};

    const EnvironmentMap::ID id =
        context().assetDescManager->save<EnvironmentMap>(std::move(environment));
    LOG_INFO("Import environment '{}' saved as '{}'", source.string(), id.value);
    return id;
}

EnvironmentMap::ID AssetImporter::importEnvironmentMap(const ImportEnvironmentMapSetting& setting)
{
    auto prepared = prepareEnvironmentMap(setting);
    CHECK(prepared.has_value(), "environment import was canceled: {}", setting.source.string());
    return saveEnvironmentMap(std::move(*prepared));
}
