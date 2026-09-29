#include "asset/AssetImporter.h"

#include "asset/AssetDataManager.h"
#include "asset/TextureMip.h"
#include "asset/import/EnvironmentMapUtils.h"
#include "core/Logger.h"
#include "core/math/Sampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <numbers>
#include <stop_token>
#include <thread>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace
{
using asset_import::environment_map::faceDirection;
using asset_import::environment_map::LinearCubemapView;
using asset_import::environment_map::linearCubemapView;
using asset_import::environment_map::mipLevelForSolidAngle;
using asset_import::environment_map::sampleRadiance;
using asset_import::environment_map::writePixel;

template <class RenderRow>
void parallelFaceRows(const char* stage, uint32_t mip, uint32_t side, std::stop_token stop, RenderRow&& renderRow)
{
    const uint32_t     totalRows  = side * 6;
    const uint32_t     hardware   = std::thread::hardware_concurrency();
    constexpr uint32_t maxWorkers = 32;
    const uint32_t     workerCount =
        totalRows < 32 ? 1u : std::min({totalRows, maxWorkers, std::max(1u, hardware > 1 ? hardware - 1 : 1u)});
    LOG_INFO("{} mip {} using {} CPU threads for {} face rows", stage, mip, workerCount, totalRows);

    std::atomic<uint32_t> nextRow       = 0;
    std::atomic<uint32_t> completedRows = 0;
    std::mutex            progressMutex;
    auto                  lastReport = std::chrono::steady_clock::now();

    const auto work = [&]
    {
        while (!stop.stop_requested())
        {
            const uint32_t row = nextRow.fetch_add(1, std::memory_order_relaxed);
            if (row >= totalRows)
            {
                break;
            }

            renderRow(row / side, row % side);
            const uint32_t completed = completedRows.fetch_add(1, std::memory_order_relaxed) + 1;
            if (progressMutex.try_lock())
            {
                std::lock_guard lock(progressMutex, std::adopt_lock);
                const auto      now = std::chrono::steady_clock::now();
                if (now - lastReport >= std::chrono::seconds{5})
                {
                    LOG_INFO("{} mip {} progress: {}/{} rows ({:.1f}%)",
                             stage,
                             mip,
                             completed,
                             totalRows,
                             100.0 * completed / totalRows);
                    lastReport = now;
                }
            }
        }
    };

    std::vector<std::jthread> threads;
    threads.reserve(workerCount - 1);
    for (uint32_t worker = 1; worker < workerCount; ++worker)
    {
        threads.emplace_back(work);
    }
    work();
}
}  // namespace

// 输入：cubemap 含 mip 0 的线性 RGBA float 和编码数据；radiance 指定格式、色彩
// 空间、面尺寸与完整 mip 数。每级在同一面内对前一级的像素区域取平均。
// 输出：两个缓冲区均追加完整 mip 链，依次排列 mip、六面、行优先 RGBA 像素。
void AssetImporter::buildMipmapChain(CubemapPixels& cubemap, const Texture::Desc& radiance, std::stop_token stop)
{
    CHECK(radiance.layout == ImageLayout::Cubemap && radiance.width > 0 && radiance.width == radiance.height &&
              radiance.mipLevels == texture_mip::maxLevels(radiance.width, radiance.height),
          "invalid radiance mip chain description: {}",
          radiance.id);

    const uint32_t side              = radiance.width;
    const uint32_t encodedPixelBytes = AssetDataManager::bytesPerPixel(radiance.format);
    const uint64_t basePixels        = static_cast<uint64_t>(side) * side * 6;
    CHECK(cubemap.linear.size() == basePixels * 4 && cubemap.encoded.size() == basePixels * encodedPixelBytes,
          "invalid radiance base pixels: {}",
          radiance.id);

    constexpr uint32_t linearPixelBytes = 4 * sizeof(float);
    const uint64_t     linearBytes      = texture_mip::totalBytes(side, side, 6, linearPixelBytes, radiance.mipLevels);
    const uint64_t     encodedBytes     = texture_mip::totalBytes(side, side, 6, encodedPixelBytes, radiance.mipLevels);
    CHECK(encodedBytes <= std::numeric_limits<uint32_t>::max() &&
              linearBytes / sizeof(float) <= std::numeric_limits<size_t>::max(),
          "radiance mip chain is too large: {}",
          radiance.id);

    cubemap.linear.resize(static_cast<size_t>(linearBytes / sizeof(float)));
    cubemap.encoded.resize(static_cast<size_t>(encodedBytes));

    for (uint32_t mip = 1; mip < radiance.mipLevels; ++mip)
    {
        if (stop.stop_requested())
        {
            return;
        }

        const uint32_t previousSide = texture_mip::extent(side, mip - 1);
        const uint32_t mipSide      = texture_mip::extent(side, mip);
        const size_t   previousOffset =
            static_cast<size_t>(texture_mip::offset(side, side, 6, linearPixelBytes, mip - 1) / sizeof(float));
        const size_t linearOffset =
            static_cast<size_t>(texture_mip::offset(side, side, 6, linearPixelBytes, mip) / sizeof(float));
        const size_t encodedOffset = static_cast<size_t>(texture_mip::offset(side, side, 6, encodedPixelBytes, mip));

        parallelFaceRows(
            "Radiance",
            mip,
            mipSide,
            stop,
            [&](uint32_t face, uint32_t y)
            {
                const uint32_t startY = static_cast<uint32_t>(static_cast<uint64_t>(y) * previousSide / mipSide);
                const uint32_t endY   = static_cast<uint32_t>(static_cast<uint64_t>(y + 1) * previousSide / mipSide);
                for (uint32_t x = 0; x < mipSide; ++x)
                {
                    const uint32_t startX = static_cast<uint32_t>(static_cast<uint64_t>(x) * previousSide / mipSide);
                    const uint32_t endX = static_cast<uint32_t>(static_cast<uint64_t>(x + 1) * previousSide / mipSide);

                    std::array<float, 4> average{};
                    for (uint32_t sourceY = startY; sourceY < endY; ++sourceY)
                    {
                        for (uint32_t sourceX = startX; sourceX < endX; ++sourceX)
                        {
                            const size_t source = previousOffset +
                                ((static_cast<size_t>(face) * previousSide + sourceY) * previousSide + sourceX) * 4;
                            for (size_t channel = 0; channel < average.size(); ++channel)
                            {
                                average[channel] += cubemap.linear[source + channel];
                            }
                        }
                    }

                    const float  weight = 1.0f / static_cast<float>((endX - startX) * (endY - startY));
                    const size_t pixel  = (static_cast<size_t>(face) * mipSide + y) * mipSide + x;
                    for (size_t channel = 0; channel < average.size(); ++channel)
                    {
                        average[channel] *= weight;
                        cubemap.linear[linearOffset + pixel * 4 + channel] = average[channel];
                    }
                    writePixel(cubemap.encoded.data() + encodedOffset + pixel * encodedPixelBytes,
                               average,
                               radiance.format,
                               radiance.colorSpace);
                }
            });
        if (stop.stop_requested())
        {
            return;
        }

        LOG_INFO("Radiance mip {}/{} ready: {}x{}x6", mip, radiance.mipLevels - 1, mipSide, mipSide);
    }
}

// 输入：已生成完整 mip 链的 radiance 描述和线性 RGBA float 像素，面顺序与描述一致。
// 输出：side x side x 6 的线性 RGBA32F Cubemap，面顺序 +X、-X、+Y、-Y、+Z、-Z，
// 每面行优先、像素内 RGBA 浮点交错、Alpha 为 1。每个方向用 sampleCount 个余弦加权
// 半球样本估计 E(n) = ∫ L(w) max(n·w) dω = π × 样本辐亮度平均值。
// 样本 PDF = cosθ/π，样本立体角 = 1/(sampleCount * PDF)，以此选择 radiance LOD。
std::vector<uint8_t> AssetImporter::irradianceCubemap(const Texture::Desc&   radiance,
                                                      std::span<const float> radiancePixels,
                                                      uint32_t               side,
                                                      uint32_t               sampleCount,
                                                      std::stop_token        stop)
{
    CHECK(side > 0 && sampleCount > 0, "invalid irradiance dimensions or sample count");
    const LinearCubemapView radianceView = linearCubemapView(radiance, radiancePixels);

    struct HemisphereSample
    {
        glm::vec3 direction;
        float     lod;
    };
    std::vector<HemisphereSample> hemisphereSamples(sampleCount);
    for (uint32_t sample = 0; sample < sampleCount; ++sample)
    {
        // 余弦重要性采样
        const auto [xi1, xi2]     = math::Sampler::HammersleySample2D(sample, sampleCount);
        const float radius        = std::sqrt(xi1);
        const float phi           = 2.0f * std::numbers::pi_v<float> * xi2;
        const float cosine        = std::sqrt(1.0f - radius * radius);
        const float pdf           = cosine / std::numbers::pi_v<float>;
        hemisphereSamples[sample] = {
            .direction =
                {
                    radius * std::cos(phi),
                    radius * std::sin(phi),
                    cosine,
                },
            .lod = mipLevelForSolidAngle(1.0f / (static_cast<float>(sampleCount) * pdf),
                                         radiance.width,
                                         radiance.mipLevels),
        };
    }

    constexpr size_t     pixelBytes = 4 * sizeof(float);
    std::vector<uint8_t> bytes(static_cast<size_t>(side) * side * 6 * pixelBytes);
    const float          scale = std::numbers::pi_v<float> / sampleCount;
    parallelFaceRows("Irradiance",
                     0,
                     side,
                     stop,
                     [&](uint32_t face, uint32_t y)
                     {
                         for (uint32_t x = 0; x < side; ++x)
                         {
                             const float     u         = 2.0f * (static_cast<float>(x) + 0.5f) / side - 1.0f;
                             const float     v         = 2.0f * (static_cast<float>(y) + 0.5f) / side - 1.0f;
                             const glm::vec3 normal    = faceDirection(face, u, v);
                             const glm::vec3 up        = std::abs(normal.y) < 0.999f ? glm::vec3{0.0f, 1.0f, 0.0f}
                                                                                     : glm::vec3{1.0f, 0.0f, 0.0f};
                             const glm::vec3 tangent   = glm::normalize(glm::cross(up, normal));
                             const glm::vec3 bitangent = glm::cross(normal, tangent);

                             std::array<float, 4> irradiance{0.0f, 0.0f, 0.0f, 1.0f};
                             for (const HemisphereSample& sample : hemisphereSamples)
                             {
                                 const glm::vec3 direction = tangent * sample.direction.x +
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

                             uint8_t* output =
                                 bytes.data() + ((static_cast<size_t>(face) * side + y) * side + x) * pixelBytes;
                             writePixel(output, irradiance, ImageFormat::RGBA32F, ColorSpace::Linear);
                         }
                     });

    return bytes;
}

// 输入：radiancePixels 是按 mip、+X、-X、+Y、-Y、+Z、-Z 排列的线性 RGBA float
// Cubemap；sampleCount 是 mip 1 每个输出方向的 GGX 样本数，更高 mip 每级翻倍，
// 直到 maxSampleCount 为止。
// 输出：RGBA32F 的完整 mip 链。按 mip 从大到小排列，每级内部依次存六个行优先面。
// mip 0 复制线性 radiance 基础层；其余级的 roughness = mip / (mipLevels - 1)。
// V=N 时入射方向的 PDF = D_GGX(N·H)/4；由 1/(sampleCount * PDF) 得样本立体角，
// 再与 radiance 基础层平均像素立体角比较得到采样 LOD。
std::vector<uint8_t> AssetImporter::prefilterSpecularMap(const Texture::Desc&   radiance,
                                                         std::span<const float> radiancePixels,
                                                         uint32_t               sampleCount,
                                                         uint32_t               maxSampleCount,
                                                         std::stop_token        stop)
{
    CHECK(sampleCount > 0 && maxSampleCount >= sampleCount,
          "prefilter sample count must be positive and no greater than its cap");

    const LinearCubemapView radianceView = linearCubemapView(radiance, radiancePixels);
    const uint32_t          side         = radianceView.side;
    const uint32_t          mipLevels    = texture_mip::maxLevels(side, side);
    const uint64_t          baseChannels = static_cast<uint64_t>(side) * side * 6 * 4;

    constexpr uint32_t pixelBytes = 4 * sizeof(float);
    const uint64_t     byteCount  = texture_mip::totalBytes(side, side, 6, pixelBytes, mipLevels);
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(), "prefiltered cubemap is too large: {}", radiance.id);

    std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
    std::memcpy(bytes.data(), radiancePixels.data(), static_cast<size_t>(baseChannels * sizeof(float)));

    uint32_t levelSampleCount = sampleCount;
    for (uint32_t mip = 1; mip < mipLevels; ++mip)
    {
        if (stop.stop_requested())
        {
            return bytes;
        }

        const auto     levelStart   = std::chrono::steady_clock::now();
        const uint32_t mipSide      = texture_mip::extent(side, mip);
        const float    roughness    = static_cast<float>(mip) / (mipLevels - 1);
        const float    alpha        = roughness * roughness;
        const float    alphaSquared = alpha * alpha;
        const size_t   levelOffset  = static_cast<size_t>(texture_mip::offset(side, side, 6, pixelBytes, mip));
        LOG_INFO("Prefilter mip {}/{} started: {}x{}x6, {} candidate samples per pixel",
                 mip,
                 mipLevels - 1,
                 mipSide,
                 mipSide,
                 levelSampleCount);

        struct PrefilterSample
        {
            glm::vec3 direction;
            float     weight;
            float     lod;
        };
        std::vector<PrefilterSample> directions;
        directions.reserve(levelSampleCount);
        for (uint32_t sample = 0; sample < levelSampleCount; ++sample)
        {
            const auto [xi1, xi2]     = math::Sampler::HammersleySample2D(sample, levelSampleCount);
            const float phi           = 2.0f * std::numbers::pi_v<float> * xi1;
            const float cosineSquared = (1.0f - xi2) / (1.0f + (alphaSquared - 1.0f) * xi2);
            const float cosine        = std::sqrt(cosineSquared);
            const float sine          = std::sqrt(std::max(0.0f, 1.0f - cosineSquared));

            // V = N。先以 GGX 分布采样半程向量 H，再反射得到入射方向 L。
            const glm::vec3 lightDirection{
                2.0f * cosine * sine * std::cos(phi),
                2.0f * cosine * sine * std::sin(phi),
                2.0f * cosineSquared - 1.0f,
            };
            if (lightDirection.z > 0.0f)
            {
                const float denominator  = cosineSquared * (alphaSquared - 1.0f) + 1.0f;
                const float distribution = alphaSquared / (std::numbers::pi_v<float> * denominator * denominator);
                const float pdf          = distribution * 0.25f;
                const float lod          = mipLevelForSolidAngle(1.0f / (static_cast<float>(levelSampleCount) * pdf),
                                                        radiance.width,
                                                        radiance.mipLevels);
                directions.push_back({lightDirection, lightDirection.z, lod});
            }
        }

        parallelFaceRows("Prefilter",
                         mip,
                         mipSide,
                         stop,
                         [&](uint32_t face, uint32_t y)
                         {
                             for (uint32_t x = 0; x < mipSide; ++x)
                             {
                                 const float     u         = 2.0f * (static_cast<float>(x) + 0.5f) / mipSide - 1.0f;
                                 const float     v         = 2.0f * (static_cast<float>(y) + 0.5f) / mipSide - 1.0f;
                                 const glm::vec3 normal    = faceDirection(face, u, v);
                                 const glm::vec3 up        = std::abs(normal.y) < 0.999f ? glm::vec3{0.0f, 1.0f, 0.0f}
                                                                                         : glm::vec3{1.0f, 0.0f, 0.0f};
                                 const glm::vec3 tangent   = glm::normalize(glm::cross(up, normal));
                                 const glm::vec3 bitangent = glm::cross(normal, tangent);

                                 std::array<float, 4> color{0.0f, 0.0f, 0.0f, 1.0f};
                                 float                totalWeight = 0.0f;
                                 for (const PrefilterSample& sample : directions)
                                 {
                                     const glm::vec3 direction = tangent * sample.direction.x +
                                         bitangent * sample.direction.y + normal * sample.direction.z;
                                     const auto radianceSample = sampleRadiance(radianceView, direction, sample.lod);
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
                         });
        if (stop.stop_requested())
        {
            return bytes;
        }

        LOG_INFO("Prefilter mip {} ready in {:.1f}s",
                 mip,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - levelStart).count());

        levelSampleCount =
            static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(levelSampleCount) * 2, maxSampleCount));
    }

    return bytes;
}
