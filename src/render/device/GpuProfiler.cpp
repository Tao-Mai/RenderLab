#include "render/device/GpuProfiler.h"

#include "core/profiling/Profiler.h"
#include "render/RenderContext.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <algorithm>
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
    timestampPools.clear();
    validBits = 0;
    device    = nullptr;
}

void GpuProfiler::discard()
{
    pending = false;
    scopes.clear();
    usedPools         = 0;
    recordingProfiler = nullptr;
}

void GpuProfiler::beginFrame(vk::raii::CommandBuffer& commands, Profiler* profiler)
{
    DCHECK(device);
    discard();
    recordingProfiler = profiler;
    if (!recordingProfiler)
        return;

    frameNumber = profiler->frameNumber();
    if (validBits)
        writeTimestamp(commands, 0, vk::PipelineStageFlagBits2::eTopOfPipe);
}

void GpuProfiler::writeTimestamp(vk::raii::CommandBuffer& commands, uint32_t query, vk::PipelineStageFlagBits2 stage)
{
    const auto index = query / kQueriesPerPool;
    DCHECK(index <= usedPools);
    if (index == usedPools)
    {
        if (index == timestampPools.size())
            timestampPools.push_back(vkCheck(device->createQueryPool({
                .queryType  = vk::QueryType::eTimestamp,
                .queryCount = kQueriesPerPool,
            })));
        commands.resetQueryPool(*timestampPools[index], 0, kQueriesPerPool);
        ++usedPools;
    }
    commands.writeTimestamp2(stage, *timestampPools[index], query % kQueriesPerPool);
}

void GpuProfiler::endFrame(vk::raii::CommandBuffer& commands)
{
    if (!recordingProfiler)
        return;
    recordingProfiler = nullptr;
    if (!validBits)
        return;
    writeTimestamp(commands, 1, vk::PipelineStageFlagBits2::eBottomOfPipe);
    pending = true;
}

void GpuProfiler::collect(Profiler* profiler)
{
    if (!pending || !profiler)
        return;
    const uint32_t        count = 2 + 2 * static_cast<uint32_t>(scopes.size());
    std::vector<uint64_t> values;
    values.reserve(count);
    for (uint32_t index = 0; index < usedPools; ++index)
    {
        const auto queryCount = std::min(count - index * kQueriesPerPool, kQueriesPerPool);
        auto       result     = timestampPools[index].getResults<uint64_t>(
            0,
            queryCount,
            queryCount * sizeof(uint64_t),
            sizeof(uint64_t),
            vk::QueryResultFlagBits::e64
        );
        if (result.result == vk::Result::eNotReady)
            return;
        vkCheck(result.result);
        values.insert(values.end(), result.value.begin(), result.value.end());
    }

    const uint64_t mask    = validBits == 64 ? ~uint64_t{0} : (uint64_t{1} << validBits) - 1;
    const auto     elapsed = [&](uint32_t first, uint32_t last)
    { return static_cast<double>((values[last] - values[first]) & mask) * timestampPeriod / 1e6; };
    std::vector<ProfileTiming> timings;
    timings.reserve(scopes.size());
    for (const bool renderPass : {true, false})
        for (uint32_t index = 0; index < scopes.size(); ++index)
            if (scopes[index].renderPass == renderPass)
                timings.push_back({scopes[index].name, elapsed(2 + 2 * index, 3 + 2 * index)});
    profiler->recordGpu(frameNumber, elapsed(0, 1), std::move(timings));
    pending = false;
}

uint32_t GpuProfiler::beginScope(vk::raii::CommandBuffer& commands, std::string_view name, bool renderPass)
{
    if (!recordingProfiler || !validBits)
        return ~0u;
    DCHECK(scopes.size() < (std::numeric_limits<uint32_t>::max() - 2) / 2);
    const uint32_t index = static_cast<uint32_t>(scopes.size());
    scopes.push_back({std::string{name}, renderPass});
    const auto stage = renderPass ? vk::PipelineStageFlagBits2::eTopOfPipe : vk::PipelineStageFlagBits2::eAllCommands;
    writeTimestamp(commands, 2 + 2 * index, stage);
    return index;
}

void GpuProfiler::endScope(vk::raii::CommandBuffer& commands, uint32_t index)
{
    if (index != ~0u)
    {
        const auto stage = scopes[index].renderPass ? vk::PipelineStageFlagBits2::eBottomOfPipe
                                                    : vk::PipelineStageFlagBits2::eAllCommands;
        writeTimestamp(commands, 3 + 2 * index, stage);
    }
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

namespace Falcor
{
ScopedProfilerEvent::ScopedProfilerEvent(RenderContext* context, std::string_view name) :
    mpRenderContext(context), cpuTiming(context->profiler, name)
{
    if (auto* profiler = context->profiler)
        profiler->beginPass(name);
    if (auto* profiler = context->gpuProfiler)
    {
        DCHECK(context->commands);
        gpuScope = profiler->beginScope(*context->commands, name, true);
    }
}

ScopedProfilerEvent::~ScopedProfilerEvent()
{
    if (gpuScope != ~0u)
        mpRenderContext->gpuProfiler->endScope(*mpRenderContext->commands, gpuScope);
}
}  // namespace Falcor
