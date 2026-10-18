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
    timestampPool     = nullptr;
    timestampCapacity = 0;
    validBits         = 0;
    device            = nullptr;
}

void GpuProfiler::discard()
{
    pending = false;
    passNames.clear();
    scopes.clear();
    recordingProfiler = nullptr;
}

void GpuProfiler::beginFrame(
    vk::raii::CommandBuffer& commands, std::span<const std::string_view> names, Profiler* profiler
)
{
    DCHECK(device);
    discard();
    recordingProfiler = profiler;
    if (!recordingProfiler)
        return;

    DCHECK(!names.empty());
    DCHECK(names.size() < (std::numeric_limits<uint32_t>::max() - 2) / 2);
    passNames.assign(names.begin(), names.end());
    frameNumber              = profiler->frameNumber();
    const uint32_t passCount = static_cast<uint32_t>(names.size());
    if (!validBits)
        return;
    const uint32_t count = 2 + 2 * (passCount + kMaxScopes);
    if (count > timestampCapacity)
    {
        timestampPool = vkCheck(device->createQueryPool({.queryType = vk::QueryType::eTimestamp, .queryCount = count}));
        timestampCapacity = count;
    }
    commands.resetQueryPool(*timestampPool, 0, count);
    commands.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *timestampPool, 0);
}

void GpuProfiler::beginPass(vk::raii::CommandBuffer& commands, uint32_t index)
{
    if (!recordingProfiler)
        return;
    DCHECK(index < passNames.size());
    if (validBits)
        commands.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *timestampPool, 2 + index * 2);
}
void GpuProfiler::endPass(vk::raii::CommandBuffer& commands, uint32_t index) const
{
    if (!recordingProfiler)
        return;
    if (validBits)
        commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *timestampPool, 3 + index * 2);
}
void GpuProfiler::endFrame(vk::raii::CommandBuffer& commands)
{
    if (!recordingProfiler)
        return;
    recordingProfiler = nullptr;
    if (!validBits)
        return;
    commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *timestampPool, 1);
    pending = true;
}

void GpuProfiler::collect(Profiler* profiler)
{
    if (!pending || !profiler)
        return;
    if (validBits)
    {
        const uint32_t count  = 2 + 2 * static_cast<uint32_t>(passNames.size() + scopes.size());
        auto           result = timestampPool.getResults<uint64_t>(
            0,
            count,
            count * sizeof(uint64_t),
            sizeof(uint64_t),
            vk::QueryResultFlagBits::e64
        );
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
    }

    pending = false;
}

uint32_t GpuProfiler::beginScope(vk::raii::CommandBuffer& commands, std::string_view name)
{
    if (!recordingProfiler || !validBits)
        return ~0u;
    DCHECK(scopes.size() < kMaxScopes);
    const uint32_t index = static_cast<uint32_t>(scopes.size());
    const uint32_t begin = 2 + 2 * static_cast<uint32_t>(passNames.size() + scopes.size());
    scopes.push_back({std::string{name}, begin, begin + 1});
    commands.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *timestampPool, begin);
    return index;
}

void GpuProfiler::endScope(vk::raii::CommandBuffer& commands, uint32_t index) const
{
    if (index != ~0u)
        commands.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *timestampPool, scopes[index].end);
}

GpuProfileScope::GpuProfileScope(GpuProfiler* targetProfiler, vk::raii::CommandBuffer& target, std::string_view name) :
    commands(target), profiler(targetProfiler)
{
    if (profiler)
        scope = profiler->beginScope(commands, name);
}

GpuProfileScope::~GpuProfileScope()
{
    if (profiler)
        profiler->endScope(commands, scope);
}
