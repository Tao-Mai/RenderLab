#include "render/device/GpuProfiler.h"

#include <slang-rhi.h>
#include "core/profiling/Profiler.h"
#include "render/RenderContext.h"
#include "render/device/RhiCheck.h"

#include <algorithm>
#include <limits>

void GpuProfiler::init(rhi::IDevice& target)
{

    device               = &target;
    const auto frequency = device->getInfo().timestampFrequency;
    validBits            = frequency ? 64 : 0;
    timestampPeriod      = frequency ? 1e9 / double(frequency) : 0;
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

void GpuProfiler::beginFrame(rhi::ICommandEncoder& commands, Profiler* profiler)
{

    discard();
    recordingProfiler = profiler;
    if (!recordingProfiler)
        return;

    frameNumber = profiler->frameNumber();
    if (validBits)
        writeTimestamp(commands, 0);
}

void GpuProfiler::writeTimestamp(rhi::ICommandEncoder& commands, uint32_t query)
{
    const auto index = query / kQueriesPerPool;
    if (index == usedPools)
    {
        if (index == timestampPools.size())
        {
            rhi::QueryPoolDesc desc;
            desc.type  = rhi::QueryType::Timestamp;
            desc.count = kQueriesPerPool;
            rhi::ComPtr<rhi::IQueryPool> pool;
            rhiCheck(device->createQueryPool(desc, pool.writeRef()));
            timestampPools.push_back(pool);
        }
        rhiCheck(timestampPools[index]->reset());
        ++usedPools;
    }
    commands.writeTimestamp(timestampPools[index], query % kQueriesPerPool);
}

void GpuProfiler::endFrame(rhi::ICommandEncoder& commands)
{
    if (!recordingProfiler)
        return;
    recordingProfiler = nullptr;
    if (!validBits)
        return;
    writeTimestamp(commands, 1);
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
        const auto offset     = values.size();
        values.resize(offset + queryCount);
        rhiCheck(timestampPools[index]->getResult(0, queryCount, values.data() + offset));
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

uint32_t GpuProfiler::beginScope(rhi::ICommandEncoder& commands, std::string_view name, bool renderPass)
{
    if (!recordingProfiler || !validBits)
        return ~0u;

    const uint32_t index = static_cast<uint32_t>(scopes.size());
    scopes.push_back({std::string{name}, renderPass});
    writeTimestamp(commands, 2 + 2 * index);
    return index;
}

void GpuProfiler::endScope(rhi::ICommandEncoder& commands, uint32_t index)
{
    if (index != ~0u)
    {
        writeTimestamp(commands, 3 + 2 * index);
    }
}

GpuProfileScope::GpuProfileScope(GpuProfiler* targetProfiler, rhi::ICommandEncoder& target, std::string_view name) :
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

        gpuScope = profiler->beginScope(*context->commands, name, true);
    }
}

ScopedProfilerEvent::~ScopedProfilerEvent()
{
    if (gpuScope != ~0u)
        mpRenderContext->gpuProfiler->endScope(*mpRenderContext->commands, gpuScope);
}
}  // namespace Falcor
