#include "asset/AssetImporter.h"

#include "asset/TextureMip.h"
#include "asset/import/EnvironmentMapUtils.h"
#include "core/Logger.h"

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
void AssetImporter::buildMipmapChain(CubemapPixels& cubemap, const TextureAsset& radiance, std::stop_token stop)
{
    CHECK(radiance.layout == ImageLayout::Cubemap && radiance.width > 0 && radiance.width == radiance.height &&
              radiance.mipLevels == texture_mip::maxLevels(radiance.width, radiance.height),
          "invalid radiance mip chain description: {}",
          radiance.id);

    const uint32_t side              = radiance.width;
    const ImageBlock encodedBlock = imageBlock(radiance.format);
    const uint32_t encodedPixelBytes = encodedBlock.bytes;
    const uint64_t basePixels        = static_cast<uint64_t>(side) * side * 6;
    CHECK(cubemap.linear.size() == basePixels * 4 && cubemap.encoded.size() == basePixels * encodedPixelBytes,
          "invalid radiance base pixels: {}",
          radiance.id);

    constexpr ImageBlock kLinearBlock = imageBlock(ImageFormat::RGBA32F);
    const uint64_t linearBytes  = texture_mip::totalBytes(side, side, 6, kLinearBlock, radiance.mipLevels);
    const uint64_t encodedBytes = texture_mip::totalBytes(side, side, 6, encodedBlock, radiance.mipLevels);
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
            static_cast<size_t>(texture_mip::offset(side, side, 6, kLinearBlock, mip - 1) / sizeof(float));
        const size_t linearOffset =
            static_cast<size_t>(texture_mip::offset(side, side, 6, kLinearBlock, mip) / sizeof(float));
        const size_t encodedOffset = static_cast<size_t>(texture_mip::offset(side, side, 6, encodedBlock, mip));

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
