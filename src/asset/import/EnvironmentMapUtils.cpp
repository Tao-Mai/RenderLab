#include "asset/import/EnvironmentMapUtils.h"

#include "asset/AssetDataManager.h"
#include "asset/TextureMip.h"
#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>

namespace asset_import::environment_map
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
[[nodiscard]] static std::array<float, 4> sampleCubemapLevel(
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

// 输入：radiancePixels 是完整线性 RGBA float mip 链，按 mip、六面、行优先像素排列。
// 输出：各 mip 的起始指针；同时验证描述与缓冲区大小一致。
[[nodiscard]] LinearCubemapView linearCubemapView(
    const TextureAsset& radiance, std::span<const float> radiancePixels)
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
[[nodiscard]] static float encodeSrgb(float linear)
{
    return linear <= 0.0031308f
        ? linear * 12.92f
        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// 输入/输出：8 位通道归一化后的 sRGB 强度 [0, 1] -> 线性强度 [0, 1]。
[[nodiscard]] static float decodeSrgb(float encoded)
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
