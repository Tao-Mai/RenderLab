#include "asset/AssetImporter.h"

#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/import/ImportId.h"
#include "asset/TextureMip.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/math/Sampler.h"

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

// 执行流程：loadImage 解码源文件 -> environmentProjection 验证经纬图或竖排 Cube ->
// environmentCubemap 生成 radiance 六面图 -> buildMipmapChain 生成 radiance mip 链 ->
// irradianceCubemap 做余弦加权半球积分 ->
// prefilterSpecularMap 生成 GGX 预滤波 mip 链 -> 保存纹理及 EnvironmentMap::Desc。
namespace
{
// 输入：face 为 0..5，依次表示 +X、-X、+Y、-Y、+Z、-Z；u、v 为面内 [-1, 1] 坐标。
// 输出：对应面上从原点指向该像素的单位三维方向，无像素缓冲区。
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

// 输入：image 指向 width * height * 4 个连续 float，按行优先排列，像素内为 RGBA；
// direction 是单位方向。将方向映射为经纬图坐标，水平循环、竖直夹紧后双线性采样。
// 输出：一个线性 RGBA 浮点像素，通道顺序为 R、G、B、A。
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

    // 经度在图像左右边界连续，横坐标需要循环。
    const auto wrapX = [width](int value)
    {
        const int size = static_cast<int>(width);
        return static_cast<uint32_t>((value % size + size) % size);
    };

    // 读取源图像指定通道的浮点值。
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

// 输入：image 指向 side * (6 * side) * 4 个行优先 float，像素内为 RGBA；
// 源图从上到下竖排 +X、-X、+Y、-Y、+Z、-Z 六个 side x side 的面。
// face、x、y 指目标 Cubemap 的面号和面内像素坐标；按面翻转坐标以匹配目标方向。
// 输出：一个线性 RGBA 浮点像素，不修改源数据。
[[nodiscard]] std::array<float, 4> sampleOpenExrCube(
    const float* image, uint32_t side, uint32_t face, uint32_t x, uint32_t y)
{
    const bool     flipVertical = face == 2 || face == 3;
    const uint32_t sourceX      = flipVertical ? x : side - 1 - x;
    const uint32_t sourceY      = flipVertical ? side - 1 - y : y;
    const float*   source       = image +
        ((static_cast<size_t>(face) * side + sourceY) * side + sourceX) * 4;
    return {source[0], source[1], source[2], source[3]};
}

// 输入：六面连续的线性 RGBA float Cubemap 和单位方向；每面 side x side、行优先。
// 输出：按 +X、-X、+Y、-Y、+Z、-Z 面顺序查找并双线性采样得到的 RGBA 像素。
[[nodiscard]] std::array<float, 4> sampleCubemapLevel(
    const float* pixels, uint32_t side, const glm::vec3& direction)
{
    const float ax = std::abs(direction.x);
    const float ay = std::abs(direction.y);
    const float az = std::abs(direction.z);

    uint32_t face;
    float    u;
    float    v;
    if (ax >= ay && ax >= az)
    {
        face = direction.x >= 0.0f ? 0 : 1;
        u    = (direction.x >= 0.0f ? -direction.z : direction.z) / ax;
        v    = -direction.y / ax;
    }
    else if (ay >= az)
    {
        face = direction.y >= 0.0f ? 2 : 3;
        u    = direction.x / ay;
        v    = (direction.y >= 0.0f ? direction.z : -direction.z) / ay;
    }
    else
    {
        face = direction.z >= 0.0f ? 4 : 5;
        u    = (direction.z >= 0.0f ? direction.x : -direction.x) / az;
        v    = -direction.y / az;
    }

    const float    maxPixel = static_cast<float>(side - 1);
    const float    px       = std::clamp((u + 1.0f) * 0.5f * side - 0.5f, 0.0f, maxPixel);
    const float    py       = std::clamp((v + 1.0f) * 0.5f * side - 0.5f, 0.0f, maxPixel);
    const uint32_t x0       = static_cast<uint32_t>(px);
    const uint32_t y0       = static_cast<uint32_t>(py);
    const uint32_t x1       = std::min(x0 + 1, side - 1);
    const uint32_t y1       = std::min(y0 + 1, side - 1);

    const float* upperLeft = pixels +
        ((static_cast<size_t>(face) * side + y0) * side + x0) * 4;
    const float* upperRight = pixels +
        ((static_cast<size_t>(face) * side + y0) * side + x1) * 4;
    const float* lowerLeft = pixels +
        ((static_cast<size_t>(face) * side + y1) * side + x0) * 4;
    const float* lowerRight = pixels +
        ((static_cast<size_t>(face) * side + y1) * side + x1) * 4;

    std::array<float, 4> rgba{};
    for (size_t channel = 0; channel < rgba.size(); ++channel)
    {
        const float upper = std::lerp(upperLeft[channel], upperRight[channel], px - x0);
        const float lower = std::lerp(lowerLeft[channel], lowerRight[channel], px - x0);
        rgba[channel]     = std::lerp(upper, lower, py - y0);
    }

    return rgba;
}

struct LinearCubemapView
{
    uint32_t                     side;
    uint32_t                     mipLevels;
    std::array<const float*, 32> levels{};
};

// 输入：radiancePixels 是完整线性 RGBA float mip 链，按 mip、六面、行优先像素排列。
// 输出：各 mip 的起始指针；同时验证描述与缓冲区大小一致。
[[nodiscard]] LinearCubemapView linearCubemapView(
    const Texture::Desc& radiance, std::span<const float> radiancePixels)
{
    CHECK(radiance.layout == ImageLayout::Cubemap &&
          radiance.width > 0 && radiance.width == radiance.height &&
          radiance.mipLevels > 0 &&
          radiance.mipLevels <= texture_mip::maxLevels(radiance.width, radiance.height),
          "invalid radiance cubemap dimensions or mip count: {}",
          radiance.id);

    constexpr uint32_t pixelBytes = 4 * sizeof(float);
    const uint64_t expectedBytes = texture_mip::totalBytes(
        radiance.width, radiance.height, 6, pixelBytes, radiance.mipLevels);
    CHECK(radiancePixels.size() == expectedBytes / sizeof(float),
          "invalid radiance cubemap mip pixels: {}",
          radiance.id);

    LinearCubemapView view{
        .side = radiance.width,
        .mipLevels = radiance.mipLevels,
    };
    for (uint32_t mip = 0; mip < view.mipLevels; ++mip)
    {
        const size_t offset = static_cast<size_t>(texture_mip::offset(
            view.side, view.side, 6, pixelBytes, mip) / sizeof(float));
        view.levels[mip] = radiancePixels.data() + offset;
    }
    return view;
}

// 输入：sampleSolidAngle 是单个积分样本覆盖的球面立体角；side 是 radiance 基础面尺寸。
// 输出：相对平均 Cubemap 像素立体角 4π/(6*side²) 的 log4 比值，限制到有效 mip 范围。
[[nodiscard]] float mipLevelForSolidAngle(
    float sampleSolidAngle, uint32_t side, uint32_t mipLevels)
{
    const float texelSolidAngle = 4.0f * std::numbers::pi_v<float> /
        (6.0f * static_cast<float>(side) * static_cast<float>(side));
    const float lod = 0.5f * std::log2(sampleSolidAngle / texelSolidAngle);
    return std::clamp(lod, 0.0f, static_cast<float>(mipLevels - 1));
}

// 输入：完整的线性 radiance mip 链、方向与连续 LOD。
// 输出：先在各相邻 mip 的单面内双线性采样，再沿 mip 轴线性插值得到 RGBA。
[[nodiscard]] std::array<float, 4> sampleRadiance(
    const LinearCubemapView& cubemap, const glm::vec3& direction, float lod)
{
    const float clamped = std::clamp(lod, 0.0f, static_cast<float>(cubemap.mipLevels - 1));
    const uint32_t lowerMip = static_cast<uint32_t>(clamped);
    const uint32_t upperMip = std::min(lowerMip + 1, cubemap.mipLevels - 1);
    std::array<float, 4> rgba = sampleCubemapLevel(
        cubemap.levels[lowerMip], texture_mip::extent(cubemap.side, lowerMip), direction);
    if (lowerMip != upperMip)
    {
        const auto upper = sampleCubemapLevel(
            cubemap.levels[upperMip], texture_mip::extent(cubemap.side, upperMip), direction);
        const float weight = clamped - static_cast<float>(lowerMip);
        for (size_t channel = 0; channel < rgba.size(); ++channel)
        {
            rgba[channel] = std::lerp(rgba[channel], upper[channel], weight);
        }
    }
    return rgba;
}

// 输入：loadImage 得到的 ImageFormat。输出：Cubemap 的四通道存储格式；
// RGB8 扩展为 RGBA8，其余支持的 RGBA 格式保持原来的每通道位宽。
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

// 输入/输出：线性强度 [0, 1] -> sRGB 强度 [0, 1]；只用于 RGB，不用于 Alpha。
[[nodiscard]] float encodeSrgb(float linear)
{
    return linear <= 0.0031308f
        ? linear * 12.92f
        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// 输入/输出：8 位通道归一化后的 sRGB 强度 [0, 1] -> 线性强度 [0, 1]。
[[nodiscard]] float decodeSrgb(float encoded)
{
    return encoded <= 0.04045f
        ? encoded / 12.92f
        : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

// 输入：rgba 是线性 RGBA 浮点像素；output 指向至少 bytesPerPixel(format) 字节。
// 输出：在 output 原位写入交错的 RGBA8、RGBA16F 或 RGBA32F；8 位 RGB 按
// colorSpace 编码，Alpha 保持线性；16F 为四个连续的 IEEE 754 binary16，32F 为四个连续的
// 32 位 float，通道顺序均为 R、G、B、A，字节序使用运行平台的本机字节序。
void writePixel(uint8_t*    output, const std::array<float, 4>& rgba,
                ImageFormat format, ColorSpace                  colorSpace)
{
    if (format == ImageFormat::RGBA8)
    {
        for (size_t channel = 0; channel < 4; ++channel)
        {
            const float linear  = std::clamp(rgba[channel], 0.0f, 1.0f);
            const float encoded = channel == 3 || colorSpace == ColorSpace::Linear
                ? linear
                : encodeSrgb(linear);
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

// 输入：pixels 为 width * height 个行优先、交错 RGB8 或 RGBA8 像素；format
// 决定每像素是 3 还是 4 字节。RGB 按 colorSpace 解码，Alpha 不做伽马变换。
// 输出：width * height * 4 个连续 float，仍为行优先、像素内线性 RGBA；
// 三通道输入补 Alpha = 1。
[[nodiscard]] std::vector<float> decode8BitPixels(
    std::span<const uint8_t> pixels, uint32_t   width, uint32_t height,
    ImageFormat              format, ColorSpace colorSpace)
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
            linearPixels[pixel * 4 + channel] = colorSpace == ColorSpace::Srgb
                ? decodeSrgb(encoded)
                : encoded;
        }
        linearPixels[pixel * 4 + 3] = channels == 4
            ? static_cast<float>(pixels[pixel * channels + 3]) / 255.0f
            : 1.0f;
    }

    return linearPixels;
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
    CHECK(image.fileFormat == LoadedImage::FileFormat::Exr ||
          image.format == ImageFormat::RGB8 || image.format == ImageFormat::RGBA8,
          "8-bit environment images require RGB or RGBA channels: {}",
          source.string());

    return projection;
}

// 输入：image.pixels 为行优先源图；EXR 始终是每像素交错 RGBA 的 float[4]，
// 普通图像是交错 RGB8/RGBA8。projection 决定按经纬图重采样或从竖排 EXR
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

// 输入：cubemap 含 mip 0 的线性 RGBA float 和编码数据；radiance 指定格式、色彩
// 空间、面尺寸与完整 mip 数。每级在同一面内对前一级的像素区域取平均。
// 输出：两个缓冲区均追加完整 mip 链，依次排列 mip、六面、行优先 RGBA 像素。
void AssetImporter::buildMipmapChain(
    CubemapPixels& cubemap, const Texture::Desc& radiance)
{
    CHECK(radiance.layout == ImageLayout::Cubemap &&
          radiance.width > 0 && radiance.width == radiance.height &&
          radiance.mipLevels == texture_mip::maxLevels(radiance.width, radiance.height),
          "invalid radiance mip chain description: {}", radiance.id);

    const uint32_t side = radiance.width;
    const uint32_t encodedPixelBytes = AssetDataManager::bytesPerPixel(radiance.format);
    const uint64_t basePixels = static_cast<uint64_t>(side) * side * 6;
    CHECK(cubemap.linear.size() == basePixels * 4 &&
          cubemap.encoded.size() == basePixels * encodedPixelBytes,
          "invalid radiance base pixels: {}", radiance.id);

    constexpr uint32_t linearPixelBytes = 4 * sizeof(float);
    const uint64_t linearBytes = texture_mip::totalBytes(
        side, side, 6, linearPixelBytes, radiance.mipLevels);
    const uint64_t encodedBytes = texture_mip::totalBytes(
        side, side, 6, encodedPixelBytes, radiance.mipLevels);
    CHECK(encodedBytes <= std::numeric_limits<uint32_t>::max() &&
          linearBytes / sizeof(float) <= std::numeric_limits<size_t>::max(),
          "radiance mip chain is too large: {}", radiance.id);

    cubemap.linear.resize(static_cast<size_t>(linearBytes / sizeof(float)));
    cubemap.encoded.resize(static_cast<size_t>(encodedBytes));

    for (uint32_t mip = 1; mip < radiance.mipLevels; ++mip)
    {
        const uint32_t previousSide = texture_mip::extent(side, mip - 1);
        const uint32_t mipSide = texture_mip::extent(side, mip);
        const size_t previousOffset = static_cast<size_t>(texture_mip::offset(
            side, side, 6, linearPixelBytes, mip - 1) / sizeof(float));
        const size_t linearOffset = static_cast<size_t>(texture_mip::offset(
            side, side, 6, linearPixelBytes, mip) / sizeof(float));
        const size_t encodedOffset = static_cast<size_t>(texture_mip::offset(
            side, side, 6, encodedPixelBytes, mip));

        for (uint32_t face = 0; face < 6; ++face)
        {
            for (uint32_t y = 0; y < mipSide; ++y)
            {
                const uint32_t startY = static_cast<uint32_t>(
                    static_cast<uint64_t>(y) * previousSide / mipSide);
                const uint32_t endY = static_cast<uint32_t>(
                    static_cast<uint64_t>(y + 1) * previousSide / mipSide);
                for (uint32_t x = 0; x < mipSide; ++x)
                {
                    const uint32_t startX = static_cast<uint32_t>(
                        static_cast<uint64_t>(x) * previousSide / mipSide);
                    const uint32_t endX = static_cast<uint32_t>(
                        static_cast<uint64_t>(x + 1) * previousSide / mipSide);

                    std::array<float, 4> average{};
                    for (uint32_t sourceY = startY; sourceY < endY; ++sourceY)
                    {
                        for (uint32_t sourceX = startX; sourceX < endX; ++sourceX)
                        {
                            const size_t source = previousOffset +
                                ((static_cast<size_t>(face) * previousSide + sourceY) *
                                    previousSide + sourceX) * 4;
                            for (size_t channel = 0; channel < average.size(); ++channel)
                            {
                                average[channel] += cubemap.linear[source + channel];
                            }
                        }
                    }

                    const float weight = 1.0f /
                        static_cast<float>((endX - startX) * (endY - startY));
                    const size_t pixel =
                        (static_cast<size_t>(face) * mipSide + y) * mipSide + x;
                    for (size_t channel = 0; channel < average.size(); ++channel)
                    {
                        average[channel] *= weight;
                        cubemap.linear[linearOffset + pixel * 4 + channel] = average[channel];
                    }
                    writePixel(cubemap.encoded.data() +
                                   encodedOffset + pixel * encodedPixelBytes,
                               average, radiance.format, radiance.colorSpace);
                }
            }
        }
    }
}

// 输入：已生成完整 mip 链的 radiance 描述和线性 RGBA float 像素，面顺序与描述一致。
// 输出：side x side x 6 的线性 RGBA32F Cubemap，面顺序 +X、-X、+Y、-Y、+Z、-Z，
// 每面行优先、像素内 RGBA 浮点交错、Alpha 为 1。每个方向用 sampleCount 个余弦加权
// 半球样本估计 E(n) = ∫ L(w) max(n·w) dω = π × 样本辐亮度平均值。
// 样本 PDF = cosθ/π，样本立体角 = 1/(sampleCount * PDF)，以此选择 radiance LOD。
std::vector<uint8_t> AssetImporter::irradianceCubemap(
    const Texture::Desc&   radiance,
    std::span<const float> radiancePixels,
    uint32_t               side,
    uint32_t               sampleCount
    )
{
    CHECK(side > 0 && sampleCount > 0,
          "invalid irradiance dimensions or sample count");
    const LinearCubemapView radianceView = linearCubemapView(radiance, radiancePixels);

    struct HemisphereSample
    {
        glm::vec3 direction;
        float lod;
    };
    std::vector<HemisphereSample> hemisphereSamples(sampleCount);
    for (uint32_t sample = 0; sample < sampleCount; ++sample)
    {
        // 余弦重要性采样
        const auto  [xi1, xi2]    = math::Sampler::HammersleySample2D(sample, sampleCount);
        const float radius        = std::sqrt(xi1);
        const float phi           = 2.0f * std::numbers::pi_v<float> * xi2;
        const float cosine = std::sqrt(1.0f - radius * radius);
        const float pdf = cosine / std::numbers::pi_v<float>;
        hemisphereSamples[sample] = {
            .direction = {
                radius * std::cos(phi),
                radius * std::sin(phi),
                cosine,
            },
            .lod = mipLevelForSolidAngle(
                1.0f / (static_cast<float>(sampleCount) * pdf),
                radiance.width,
                radiance.mipLevels),
        };
    }

    constexpr size_t     pixelBytes = 4 * sizeof(float);
    std::vector<uint8_t> bytes(
        static_cast<size_t>(side) * side * 6 * pixelBytes);
    const float scale = std::numbers::pi_v<float> / sampleCount;

    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < side; ++y)
        {
            for (uint32_t x = 0; x < side; ++x)
            {
                const float     u      = 2.0f * (static_cast<float>(x) + 0.5f) / side - 1.0f;
                const float     v      = 2.0f * (static_cast<float>(y) + 0.5f) / side - 1.0f;
                const glm::vec3 normal = faceDirection(face, u, v);
                const glm::vec3 up     = std::abs(normal.y) < 0.999f
                    ? glm::vec3{0.0f, 1.0f, 0.0f}
                    : glm::vec3{1.0f, 0.0f, 0.0f};
                const glm::vec3 tangent   = glm::normalize(glm::cross(up, normal));
                const glm::vec3 bitangent = glm::cross(normal, tangent);

                std::array<float, 4> irradiance{0.0f, 0.0f, 0.0f, 1.0f};
                for (const HemisphereSample& sample : hemisphereSamples)
                {
                    const glm::vec3 direction =
                        tangent * sample.direction.x +
                        bitangent * sample.direction.y + normal * sample.direction.z;
                    const auto rgba = sampleRadiance(radianceView, direction, sample.lod);
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        irradiance[channel] += rgba[channel];
                    }
                }

                for (size_t channel = 0; channel < 3; ++channel)
                {
                    irradiance[channel] *= scale;
                }

                uint8_t* output = bytes.data() +
                    ((static_cast<size_t>(face) * side + y) * side + x) *
                    pixelBytes;
                writePixel(output, irradiance, ImageFormat::RGBA32F, ColorSpace::Linear);
            }
        }
    }

    return bytes;
}

// 输入：radiancePixels 是按 mip、+X、-X、+Y、-Y、+Z、-Z 排列的线性 RGBA float
// Cubemap；sampleCount 是 mip 1 每个输出方向的 GGX 样本数，更高 mip 每级翻倍，
// 直到 maxSampleCount 为止。
// 输出：RGBA32F 的完整 mip 链。按 mip 从大到小排列，每级内部依次存六个行优先面。
// mip 0 复制线性 radiance 基础层；其余级的 roughness = mip / (mipLevels - 1)。
// V=N 时入射方向的 PDF = D_GGX(N·H)/4；由 1/(sampleCount * PDF) 得样本立体角，
// 再与 radiance 基础层平均像素立体角比较得到采样 LOD。
std::vector<uint8_t> AssetImporter::prefilterSpecularMap(
    const Texture::Desc& radiance, std::span<const float> radiancePixels,
    uint32_t             sampleCount, uint32_t maxSampleCount)
{
    CHECK(sampleCount > 0 && maxSampleCount >= sampleCount,
          "prefilter sample count must be positive and no greater than its cap");

    const LinearCubemapView radianceView = linearCubemapView(radiance, radiancePixels);
    const uint32_t side         = radianceView.side;
    const uint32_t mipLevels    = texture_mip::maxLevels(side, side);
    const uint64_t baseChannels = static_cast<uint64_t>(side) * side * 6 * 4;

    constexpr uint32_t pixelBytes = 4 * sizeof(float);
    const uint64_t     byteCount  = texture_mip::totalBytes(
        side,
        side,
        6,
        pixelBytes,
        mipLevels);
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
          "prefiltered cubemap is too large: {}",
          radiance.id);

    std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
    std::memcpy(bytes.data(),
                radiancePixels.data(),
                static_cast<size_t>(baseChannels * sizeof(float)));

    uint32_t levelSampleCount = sampleCount;
    for (uint32_t mip = 1; mip < mipLevels; ++mip)
    {
        const uint32_t mipSide      = texture_mip::extent(side, mip);
        const float    roughness    = static_cast<float>(mip) / (mipLevels - 1);
        const float    alpha        = roughness * roughness;
        const float    alphaSquared = alpha * alpha;
        const size_t   levelOffset  = static_cast<size_t>(texture_mip::offset(
            side,
            side,
            6,
            pixelBytes,
            mip));

        struct PrefilterSample
        {
            glm::vec3 direction;
            float weight;
            float lod;
        };
        std::vector<PrefilterSample> directions;
        directions.reserve(levelSampleCount);
        for (uint32_t sample = 0; sample < levelSampleCount; ++sample)
        {
            const auto  [xi1, xi2]    = math::Sampler::HammersleySample2D(sample, levelSampleCount);
            const float phi           = 2.0f * std::numbers::pi_v<float> * xi1;
            const float cosineSquared = (1.0f - xi2) /
                (1.0f + (alphaSquared - 1.0f) * xi2);
            const float cosine = std::sqrt(cosineSquared);
            const float sine   = std::sqrt(std::max(0.0f, 1.0f - cosineSquared));

            // V = N。先以 GGX 分布采样半程向量 H，再反射得到入射方向 L。
            const glm::vec3 lightDirection{
                2.0f * cosine * sine * std::cos(phi),
                2.0f * cosine * sine * std::sin(phi),
                2.0f * cosineSquared - 1.0f,
            };
            if (lightDirection.z > 0.0f)
            {
                const float denominator = cosineSquared * (alphaSquared - 1.0f) + 1.0f;
                const float distribution = alphaSquared /
                    (std::numbers::pi_v<float> * denominator * denominator);
                const float pdf = distribution * 0.25f;
                const float lod = mipLevelForSolidAngle(
                    1.0f / (static_cast<float>(levelSampleCount) * pdf),
                    radiance.width,
                    radiance.mipLevels);
                directions.push_back({lightDirection, lightDirection.z, lod});
            }
        }

        for (uint32_t face = 0; face < 6; ++face)
        {
            for (uint32_t y = 0; y < mipSide; ++y)
            {
                for (uint32_t x = 0; x < mipSide; ++x)
                {
                    const float     u      = 2.0f * (static_cast<float>(x) + 0.5f) / mipSide - 1.0f;
                    const float     v      = 2.0f * (static_cast<float>(y) + 0.5f) / mipSide - 1.0f;
                    const glm::vec3 normal = faceDirection(face, u, v);
                    const glm::vec3 up     = std::abs(normal.y) < 0.999f
                        ? glm::vec3{0.0f, 1.0f, 0.0f}
                        : glm::vec3{1.0f, 0.0f, 0.0f};
                    const glm::vec3 tangent   = glm::normalize(glm::cross(up, normal));
                    const glm::vec3 bitangent = glm::cross(normal, tangent);

                    std::array<float, 4> color{0.0f, 0.0f, 0.0f, 1.0f};
                    float                totalWeight = 0.0f;
                    for (const PrefilterSample& sample : directions)
                    {
                        const glm::vec3 direction =
                            tangent * sample.direction.x + bitangent * sample.direction.y +
                            normal * sample.direction.z;
                        const auto radianceSample =
                            sampleRadiance(radianceView, direction, sample.lod);
                        for (size_t channel = 0; channel < 3; ++channel)
                        {
                            color[channel] += radianceSample[channel] * sample.weight;
                        }
                        totalWeight += sample.weight;
                    }

                    if (totalWeight > 0.0f)
                    {
                        for (size_t channel = 0; channel < 3; ++channel)
                        {
                            color[channel] /= totalWeight;
                        }
                    }
                    else
                    {
                        color    = sampleRadiance(radianceView, normal, 0.0f);
                        color[3] = 1.0f;
                    }

                    const size_t pixel = (static_cast<size_t>(face) * mipSide + y) * mipSide + x;
                    writePixel(bytes.data() + levelOffset + pixel * pixelBytes,
                               color,
                               ImageFormat::RGBA32F,
                               ColorSpace::Linear);
                }
            }
        }

        levelSampleCount = static_cast<uint32_t>(std::min<uint64_t>(
            static_cast<uint64_t>(levelSampleCount) * 2,
            maxSampleCount));
    }

    return bytes;
}

// 输入：source 是 EXR 或支持的 8 位图像文件路径；setting 可指定源图与 radiance
// 的色彩空间、irradiance 的面尺寸、两种积分的采样数及 GGX 采样上限。
// 省略色彩空间时 EXR 为 Linear、
// 8 位图像为 Srgb。loadImage 返回行优先源图。
// 输出：EnvironmentMap::Desc 的资产 ID。函数选定投影和面尺寸，将六面像素按
// environmentCubemap 所述布局写入 radiance 并生成完整 mip 链；再从 radiance 计算线性
// irradiance 纹理，并生成带完整 mip 链的 GGX 预滤波纹理。环境贴图描述引用三张纹理。
EnvironmentMap::ID AssetImporter::importEnvironmentMap(
    const std::filesystem::path& source, const ImportEnvironmentMapSetting& setting)
{
    CHECK(setting.irradianceSize > 0,
          "irradiance map size must be positive: {}",
          source.string());
    CHECK(setting.irradianceSampleCount > 0,
          "irradiance sample count must be positive: {}",
          source.string());
    CHECK(setting.prefilteredSpecularSampleCount > 0,
          "prefilter sample count must be positive: {}",
          source.string());
    CHECK(setting.prefilteredSpecularMaxSampleCount >=
          setting.prefilteredSpecularSampleCount,
          "prefilter sample cap must be at least the initial sample count: {}",
          source.string());
    constexpr uint64_t pixelBytes = 4 * sizeof(float);
    constexpr uint64_t maxPayload = std::numeric_limits<uint32_t>::max();
    CHECK(setting.irradianceSize <=
          maxPayload / setting.irradianceSize / 6 / pixelBytes,
          "irradiance map is too large: {}",
          source.string());

    const LoadedImage             image      = loadImage(source);
    const LoadedImage::Projection projection = environmentProjection(image, source);
    const uint32_t                side       = projection == LoadedImage::Projection::Cube
        ? image.width
        : std::max(1u, image.height / 2);
    const ColorSpace resolvedColorSpace = setting.colorSpace.value_or(
        defaultColorSpace(image.fileFormat));

    Texture::Desc radiance{};
    radiance.id         = asset_import::nextId<Texture>(source);
    radiance.source     = Source::File;
    radiance.format     = environmentFormat(image.format);
    radiance.colorSpace = resolvedColorSpace;
    CHECK(radiance.format == ImageFormat::RGBA8 ||
          radiance.colorSpace == ColorSpace::Linear,
          "floating-point environment textures require linear color space: {}",
          source.string());
    radiance.layout    = ImageLayout::Cubemap;
    radiance.width     = side;
    radiance.height    = side;
    radiance.mipLevels = texture_mip::maxLevels(side, side);

    CubemapPixels radiancePixels = environmentCubemap(
        image,
        projection,
        side,
        radiance.format,
        radiance.colorSpace,
        source);
    buildMipmapChain(radiancePixels, radiance);
    const std::filesystem::path irradianceName = source.parent_path() /
        (source.stem().string() + "_irradiance" + source.extension().string());
    Texture::Desc irradiance{};
    irradiance.id         = asset_import::nextId<Texture>(irradianceName);
    irradiance.source     = Source::File;
    irradiance.format     = ImageFormat::RGBA32F;
    irradiance.colorSpace = ColorSpace::Linear;
    irradiance.layout     = ImageLayout::Cubemap;
    irradiance.width      = setting.irradianceSize;
    irradiance.height     = setting.irradianceSize;
    irradiance.mipLevels  = 1;

    const std::filesystem::path prefilteredName = source.parent_path() /
        (source.stem().string() + "_prefiltered_specular" + source.extension().string());
    Texture::Desc prefiltered{};
    prefiltered.id         = asset_import::nextId<Texture>(prefilteredName);
    prefiltered.source     = Source::File;
    prefiltered.format     = ImageFormat::RGBA32F;
    prefiltered.colorSpace = ColorSpace::Linear;
    prefiltered.layout     = ImageLayout::Cubemap;
    prefiltered.width      = side;
    prefiltered.height     = side;
    prefiltered.mipLevels  = texture_mip::maxLevels(side, side);

    const std::vector<uint8_t> irradianceBytes =
        irradianceCubemap(radiance,
                          radiancePixels.linear,
                          setting.irradianceSize,
                          setting.irradianceSampleCount);
    const std::vector<uint8_t> prefilteredBytes = prefilterSpecularMap(
        radiance,
        radiancePixels.linear,
        setting.prefilteredSpecularSampleCount,
        setting.prefilteredSpecularMaxSampleCount);

    const Texture::ID radianceId =
        saveTexture(std::move(radiance), source, radiancePixels.encoded);
    const Texture::ID irradianceId =
        saveTexture(std::move(irradiance), source, irradianceBytes);
    const Texture::ID prefilteredId =
        saveTexture(std::move(prefiltered), source, prefilteredBytes);

    EnvironmentMap::Desc environment{};
    environment.id       = asset_import::nextId<EnvironmentMap>(source);
    environment.radiance = TextureBinding{
        radianceId, BuiltinAssets::Sampler::linearClamp};
    environment.irradiance = TextureBinding{
        irradianceId, BuiltinAssets::Sampler::linearClamp};
    environment.prefilteredSpecular = TextureBinding{
        prefilteredId, BuiltinAssets::Sampler::linearClamp};

    return context().assetDescManager->save<EnvironmentMap>(std::move(environment));
}
