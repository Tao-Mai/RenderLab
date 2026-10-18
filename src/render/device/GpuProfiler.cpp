#include "render/device/GpuProfiler.h"

#include "core/profiling/Profiler.h"
#include "render/device/CommandRecorder.h"
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
    passStatistics.clear();
    scopes.clear();
    recordingProfiler = nullptr;
}

void GpuProfiler::beginFrame(CommandRecorder& commands, std::span<const std::string_view> names, Profiler* profiler)
{
    DCHECK(device);
    recordingProfiler = profiler;
    commands.setGpuProfiler(profiler ? this : nullptr);
    commands.setStatistics(nullptr);
    passStatistics.clear();
    scopes.clear();
    pending = false;
    if (!recordingProfiler)
        return;
    passNames.assign(names.begin(), names.end());
    frameNumber = profiler->frameNumber();
    passStatistics.reserve(names.size());
    if (!validBits)
        return;
    DCHECK(names.size() < (std::numeric_limits<uint32_t>::max() - 2) / 2);
    const uint32_t count = 2 + 2 * (static_cast<uint32_t>(names.size()) + kMaxScopes);
    if (count > capacity)
    {
        // The owner has already waited for this frame's fence before reusing it.
        pool     = vkCheck(device->createQueryPool({.queryType = vk::QueryType::eTimestamp, .queryCount = count}));
        capacity = count;
    }
    commands.resetQueryPool(*pool, 0, count);
    commands.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *pool, 0);
}

void GpuProfiler::beginPass(CommandRecorder& commands, uint32_t index)
{
    if (!recordingProfiler)
        return;
    DCHECK(index < passNames.size());
    auto& statistics = passStatistics.emplace_back(passNames[index], DrawStatistics{});
    commands.setStatistics(&statistics.draws);
    if (validBits)
        commands.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *pool, 2 + index * 2);
}
void GpuProfiler::endPass(CommandRecorder& commands, uint32_t index) const
{
    commands.setStatistics(nullptr);
    if (!recordingProfiler)
        return;
    if (validBits)
        commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *pool, 3 + index * 2);
}
void GpuProfiler::endFrame(CommandRecorder& commands)
{
    commands.setGpuProfiler(nullptr);
    commands.setStatistics(nullptr);
    if (!recordingProfiler)
        return;
    recordingProfiler->recordDraws(passStatistics);
    recordingProfiler = nullptr;
    if (!validBits)
        return;
    commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *pool, 1);
    pending = true;
}

void GpuProfiler::collect(Profiler* profiler)
{
    if (!pending || !profiler)
        return;
    const uint32_t count = 2 + 2 * static_cast<uint32_t>(passNames.size() + scopes.size());
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
    for (const auto& scope : scopes)
        timings.push_back({scope.name, elapsed(scope.begin, scope.end)});
    profiler->recordGpu(frameNumber, elapsed(0, 1), std::move(timings));
    pending = false;
}

uint32_t GpuProfiler::beginScope(CommandRecorder& commands, std::string_view name)
{
    if (!recordingProfiler || !validBits)
        return ~0u;
    DCHECK(scopes.size() < kMaxScopes);
    const uint32_t index = static_cast<uint32_t>(scopes.size());
    const uint32_t begin = 2 + 2 * static_cast<uint32_t>(passNames.size() + scopes.size());
    scopes.push_back({std::string{name}, begin, begin + 1});
    commands.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *pool, begin);
    return index;
}

void GpuProfiler::endScope(CommandRecorder& commands, uint32_t index) const
{
    if (index != ~0u)
        commands.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *pool, scopes[index].end);
}

GpuProfileScope::GpuProfileScope(CommandRecorder& target, std::string_view name)
    : commands(target), profiler(target.gpuProfiler())
{
    if (profiler)
        scope = profiler->beginScope(commands, name);
}

GpuProfileScope::~GpuProfileScope()
{
    if (profiler)
        profiler->endScope(commands, scope);
}
