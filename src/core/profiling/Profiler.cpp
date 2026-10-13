#include "core/profiling/Profiler.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

void Profiler::beginFrame()
{
    DCHECK(!capturing);
    capturing = true;
    current.cpuTimings.clear();
    current.passes.clear();
    current.views.clear();
    current.totalObjects = 0;
    frameTimer.reset();
}

void Profiler::endFrame(double milliseconds)
{
    DCHECK(capturing);
    capturing = false;
    DCHECK(std::isfinite(milliseconds) && milliseconds > 0.0);
    frameHistory.push_back(static_cast<float>(milliseconds));
    cpuHistory.push_back(frameTimer.elapsedMilliseconds());
    if (frameHistory.size() > kHistorySize)
    {
        frameHistory.pop_front();
        cpuHistory.pop_front();
    }
    if (refreshTimer.elapsedMilliseconds() < kRefreshMilliseconds && !displayed.frameHistory.empty())
        return;

    refreshTimer.reset();
    displayed = current;
    displayed.frameHistory.assign(frameHistory.begin(), frameHistory.end());
    displayed.frameMilliseconds = std::accumulate(frameHistory.begin(), frameHistory.end(), 0.0) / frameHistory.size();
    displayed.cpuMilliseconds   = std::accumulate(cpuHistory.begin(), cpuHistory.end(), 0.0) / cpuHistory.size();
    auto sorted                 = displayed.frameHistory;
    std::ranges::sort(sorted);
    const auto percentile = [&](double fraction)
    { return sorted[static_cast<size_t>(std::ceil(fraction * sorted.size())) - 1]; };
    displayed.p95Milliseconds = percentile(0.95);
    displayed.p99Milliseconds = percentile(0.99);
}

void Profiler::recordCpu(std::string_view name, double milliseconds)
{
    if (!capturing)
        return;
    const auto found = std::ranges::find(current.cpuTimings, name, &ProfileTiming::name);
    if (found == current.cpuTimings.end())
        current.cpuTimings.push_back({std::string{name}, milliseconds});
    else
        found->milliseconds += milliseconds;
}

void Profiler::recordGpu(double milliseconds, std::vector<ProfileTiming> timings)
{
    current.gpuMilliseconds = milliseconds;
    current.gpuTimings      = std::move(timings);
    current.hasGpuTimings   = true;
}

void Profiler::recordDraws(std::vector<PassDrawStatistics> passes) { current.passes = std::move(passes); }
void Profiler::recordCulling(uint32_t totalObjects, std::vector<ViewCullingStatistics> views)
{
    current.totalObjects = totalObjects;
    current.views        = std::move(views);
}

void Profiler::clearGpu()
{
    current.gpuTimings.clear();
    current.gpuMilliseconds = 0.0;
    current.hasGpuTimings   = false;
    displayed.gpuTimings.clear();
    displayed.hasGpuTimings = false;
}

void Profiler::reset()
{
    DCHECK(!capturing);
    current   = {};
    displayed = {};
    frameHistory.clear();
    cpuHistory.clear();
    refreshTimer.reset();
}

CpuProfileScope::CpuProfileScope(Profiler* target, std::string_view label) : profiler(target), name(label) {}
CpuProfileScope::~CpuProfileScope()
{
    if (profiler)
        profiler->recordCpu(name, timer.elapsedMilliseconds());
}
