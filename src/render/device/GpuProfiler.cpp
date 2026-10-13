#include "render/device/GpuProfiler.h"

#include "core/profiling/Profiler.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <limits>

void GpuProfiler::init(const VulkanContext& vulkan)
{
    DCHECK(!device);
    device = &vulkan.deviceHandle();
    validBits =
        vulkan.physicalDeviceHandle().getQueueFamilyProperties()[vulkan.graphicsQueueFamilyIndex()].timestampValidBits;
    timestampPeriod = vulkan.properties().limits.timestampPeriod;
}

void GpuProfiler::reset()
{
    discard();
    pool      = nullptr;
    capacity  = 0;
    validBits = 0;
    device    = nullptr;
}

void GpuProfiler::discard()
{
    pending = false;
    passNames.clear();
}

void GpuProfiler::beginFrame(vk::raii::CommandBuffer& commands, std::span<const std::string_view> names)
{
    DCHECK(device);
    if (!validBits)
        return;
    DCHECK(names.size() < (std::numeric_limits<uint32_t>::max() - 2) / 2);
    const uint32_t count = 2 + 2 * static_cast<uint32_t>(names.size());
    if (count > capacity)
    {
        // The owner has already waited for this frame's fence before reusing it.
        pool     = vkCheck(device->createQueryPool({.queryType = vk::QueryType::eTimestamp, .queryCount = count}));
        capacity = count;
    }
    pending = false;
    passNames.assign(names.begin(), names.end());
    commands.resetQueryPool(*pool, 0, count);
    commands.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *pool, 0);
}

void GpuProfiler::beginPass(vk::raii::CommandBuffer& commands, uint32_t index) const
{
    if (validBits)
        commands.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *pool, 2 + index * 2);
}
void GpuProfiler::endPass(vk::raii::CommandBuffer& commands, uint32_t index) const
{
    if (validBits)
        commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *pool, 3 + index * 2);
}
void GpuProfiler::endFrame(vk::raii::CommandBuffer& commands)
{
    if (!validBits)
        return;
    commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *pool, 1);
    pending = true;
}

void GpuProfiler::collect(Profiler* profiler)
{
    if (!pending || !profiler)
        return;
    const uint32_t count = 2 + 2 * static_cast<uint32_t>(passNames.size());
    auto           result =
        pool.getResults<uint64_t>(0, count, count * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64);
    if (result.result == vk::Result::eNotReady)
        return;
    vkCheck(result.result);
    const uint64_t mask    = validBits == 64 ? ~uint64_t{0} : (uint64_t{1} << validBits) - 1;
    const auto     elapsed = [&](uint32_t first, uint32_t last)
    { return static_cast<double>((result.value[last] - result.value[first]) & mask) * timestampPeriod / 1e6; };
    std::vector<ProfileTiming> timings;
    for (uint32_t index = 0; index < passNames.size(); ++index)
        timings.push_back({passNames[index], elapsed(2 + 2 * index, 3 + 2 * index)});
    profiler->recordGpu(elapsed(0, 1), std::move(timings));
    pending = false;
}
