#include "asset/import/EnvironmentMapUtils.h"

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

// 输入：rgba 是线性 RGBA 浮点像素，output 指向至少 imageBlock(format).bytes 字节。
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
    const size_t       channels   = imageBlock(format).bytes;
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
