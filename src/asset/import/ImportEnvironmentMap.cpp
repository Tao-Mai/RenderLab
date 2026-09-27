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

// 执行流程：loadImage 解码源文件 -> environmentProjection 验证经纬图或竖排 Cube ->
// environmentCubemap 生成 radiance 六面图 -> irradianceCubemap 做余弦加权半球积分 ->
// 分别保存两张纹理，再保存引用它们的 EnvironmentMapDesc。预滤波贴图暂不生成。
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
[[nodiscard]] std::array<float, 4> sampleCubemap(
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

// Van der Corput 基 2 序列：反转 32 位整数的二进制位，得到 [0, 1) 中的样本坐标。
[[nodiscard]] float radicalInverse(uint32_t bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
    bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
    bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
    return static_cast<float>(bits) * 0x1p-32f;
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

// 输入：radiance 是已生成的六面 Cubemap 描述，bytes 为与描述匹配的交错 RGBA
// 像素，面顺序 +X、-X、+Y、-Y、+Z、-Z。8 位值按 colorSpace 解码，半精度解包。
// 输出：同样面顺序与行优先布局的线性 RGBA float 数组，供方向采样使用。
[[nodiscard]] std::vector<float> decodeRadianceCubemap(
    const TextureDesc& radiance, std::span<const uint8_t> bytes)
{
    CHECK(radiance.layout == ImageLayout::Cubemap &&
          radiance.width > 0 && radiance.width == radiance.height,
          "invalid radiance cubemap dimensions: {}", radiance.id);

    const uint64_t expectedBytes = static_cast<uint64_t>(radiance.width) *
        radiance.height * 6 * AssetDataManager::bytesPerPixel(radiance.format);
    CHECK(bytes.size() == expectedBytes,
          "invalid radiance cubemap payload: {}", radiance.id);

    if (radiance.format == ImageFormat::RGBA8)
    {
        return decode8BitPixels(bytes, radiance.width, radiance.height * 6,
                                radiance.format, radiance.colorSpace);
    }

    const size_t channelCount = static_cast<size_t>(radiance.width) *
        radiance.height * 6 * 4;
    std::vector<float> pixels(channelCount);
    if (radiance.format == ImageFormat::RGBA16F)
    {
        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            uint16_t half;
            std::memcpy(&half, bytes.data() + channel * sizeof(half), sizeof(half));
            pixels[channel] = glm::unpackHalf1x16(half);
        }
    }
    else
    {
        CHECK(radiance.format == ImageFormat::RGBA32F,
              "unsupported radiance cubemap format");
        std::memcpy(pixels.data(), bytes.data(), bytes.size());
    }

    return pixels;
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
// 输出：六个 side x side 面首尾相连的字节数组，顺序为 +X、-X、+Y、-Y、+Z、-Z。
// 每面行优先、每像素 RGBA 交错；地址偏移为 ((face * side + y) * side + x)
// * bytesPerPixel(format)，没有行间距或面间距。RGBA8 按 colorSpace 编码，
// 浮点格式保持线性值。
std::vector<uint8_t> AssetImporter::environmentCubemap(
    const LoadedImage& image, LoadedImage::Projection projection, uint32_t                     side,
    ImageFormat        format, ColorSpace             colorSpace, const std::filesystem::path& source)
{
    const size_t   bytesPerPixel = AssetDataManager::bytesPerPixel(format);
    const uint64_t byteCount     = static_cast<uint64_t>(side) * side * 6 * bytesPerPixel;
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
          "imported cubemap is too large: {}",
          source.string());

    std::vector<uint8_t> bytes(byteCount);
    std::vector<float>   linearPixels;
    const float*         decoded = nullptr;

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

                uint8_t* output = bytes.data() +
                    ((static_cast<size_t>(face) * side + y) * side + x) * bytesPerPixel;
                writePixel(output, rgba, format, colorSpace);
            }
        }
    }

    return bytes;
}

// 输入：已生成的 radiance 描述和六面像素；按其格式和色彩空间解码为线性值。
// 输出：side x side x 6 的线性 RGBA32F Cubemap，面顺序 +X、-X、+Y、-Y、+Z、-Z，
// 每面行优先、像素内 RGBA 浮点交错、Alpha 为 1。每个方向用 sampleCount 个余弦加权
// 半球样本估计 E(n) = ∫ L(w) max(n·w) dω = π × 样本辐亮度平均值。
std::vector<uint8_t> AssetImporter::irradianceCubemap(
    const TextureDesc& radiance, std::span<const uint8_t> radianceBytes,
    uint32_t side, uint32_t sampleCount)
{
    const std::vector<float> linearPixels = decodeRadianceCubemap(radiance, radianceBytes);

    std::vector<glm::vec3> hemisphereSamples(sampleCount);
    for (uint32_t sample = 0; sample < sampleCount; ++sample)
    {
        const float radius = std::sqrt((static_cast<float>(sample) + 0.5f) /
            sampleCount);
        const float phi           = 2.0f * std::numbers::pi_v<float> * radicalInverse(sample);
        hemisphereSamples[sample] = {
            radius * std::cos(phi),
            radius * std::sin(phi),
            std::sqrt(1.0f - radius * radius),
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
                for (const glm::vec3& sample : hemisphereSamples)
                {
                    const glm::vec3 direction =
                        tangent * sample.x + bitangent * sample.y + normal * sample.z;
                    const auto rgba = sampleCubemap(linearPixels.data(), radiance.width, direction);
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

// 输入：source 是 EXR 或支持的 8 位图像文件路径；setting 可指定源图与 radiance
// 的色彩空间、irradiance 的面尺寸和每像素采样数。省略色彩空间时 EXR 为 Linear、
// 8 位图像为 Srgb。loadImage 返回行优先源图。
// 输出：EnvironmentMapDesc 的资产 ID。函数选定投影和面尺寸，将六面像素按
// environmentCubemap 所述布局写入 radiance；再从 radiance 计算可配置尺寸的线性
// irradiance 纹理。环境贴图描述引用这两张纹理；prefilteredSpecular 暂不生成。
AssetId AssetImporter::importEnvironmentMap(
    const std::filesystem::path& source, const ImportEnvironmentMapSetting& setting)
{
    CHECK(setting.irradianceSize > 0,
          "irradiance map size must be positive: {}",
          source.string());
    CHECK(setting.irradianceSampleCount > 0,
          "irradiance sample count must be positive: {}",
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

    TextureDesc radiance{};
    radiance.id         = asset_import::nextId<TextureDesc>(source);
    radiance.source     = Source::File;
    radiance.format     = environmentFormat(image.format);
    radiance.colorSpace = resolvedColorSpace;
    CHECK(radiance.format == ImageFormat::RGBA8 ||
          radiance.colorSpace == ColorSpace::Linear,
          "floating-point environment textures require linear color space: {}",
          source.string());
    radiance.layout = ImageLayout::Cubemap;
    radiance.width  = side;
    radiance.height = side;

    const std::vector<uint8_t> bytes = environmentCubemap(
        image,
        projection,
        side,
        radiance.format,
        radiance.colorSpace,
        source);
    const std::filesystem::path irradianceName = source.parent_path() /
        (source.stem().string() + "_irradiance" + source.extension().string());
    TextureDesc irradiance{};
    irradiance.id         = asset_import::nextId<TextureDesc>(irradianceName);
    irradiance.source     = Source::File;
    irradiance.format     = ImageFormat::RGBA32F;
    irradiance.colorSpace = ColorSpace::Linear;
    irradiance.layout     = ImageLayout::Cubemap;
    irradiance.width      = setting.irradianceSize;
    irradiance.height     = setting.irradianceSize;

    const std::vector<uint8_t> irradianceBytes =
        irradianceCubemap(radiance,
                          bytes,
                          setting.irradianceSize,
                          setting.irradianceSampleCount);

    const AssetId radianceId   = saveTexture(std::move(radiance), source, bytes);
    const AssetId irradianceId = saveTexture(std::move(irradiance), source, irradianceBytes);

    EnvironmentMapDesc environment{};
    environment.id         = asset_import::nextId<EnvironmentMapDesc>(source);
    environment.radiance   = radianceId;
    environment.irradiance = irradianceId;

    return context().assetDescManager->save(std::move(environment));
}
