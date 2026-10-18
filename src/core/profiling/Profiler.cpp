#include "core/profiling/Profiler.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <numeric>
#include <utility>

void Profiler::beginFrame()
{
    DCHECK(!capturing);
    capturing = true;
    ++frame;
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
    logTiming(frame, "wall", "Frame interval", milliseconds);
    logTiming(frame, "cpu", "Frame", cpuHistory.back());
    for (const auto& timing : current.cpuTimings)
        logTiming(frame, "cpu", timing.name, timing.milliseconds);
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

void Profiler::recordGpu(uint64_t sourceFrame, double milliseconds, std::vector<ProfileTiming> timings)
{
    logTiming(sourceFrame, "gpu", "Frame", milliseconds);
    for (const auto& timing : timings)
        logTiming(sourceFrame, "gpu", timing.name, timing.milliseconds);
    current.gpuMilliseconds = milliseconds;
    current.gpuTimings      = std::move(timings);
    current.hasGpuTimings   = true;
}

void Profiler::beginPass(std::string_view name)
{
    if (capturing)
        current.passes.push_back({std::string{name}, {}});
}

void Profiler::recordDraw(uint32_t vertexCount, uint32_t instanceCount)
{
    if (!capturing)
        return;
    DCHECK(!current.passes.empty());
    auto& draws = current.passes.back().draws;
    ++draws.drawCalls;
    draws.triangles += static_cast<uint64_t>(vertexCount / 3) * instanceCount;
}

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
    current.passes.clear();
    displayed.gpuTimings.clear();
    displayed.hasGpuTimings = false;
    displayed.passes.clear();
}

void Profiler::reset()
{
    DCHECK(!capturing);
    stopLogging();
    frame     = 0;
    current   = {};
    displayed = {};
    frameHistory.clear();
    cpuHistory.clear();
    refreshTimer.reset();
}

void Profiler::startLogging(const std::filesystem::path& path)
{
    DCHECK(!logFile.is_open());
    if (!path.parent_path().empty())
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        CHECK(!error, "cannot create profile log directory '{}': {}", path.parent_path().string(), error.message());
    }
    logFile.open(path, std::ios::trunc);
    CHECK(logFile.is_open(), "cannot open profile log '{}'", path.string());
    logFile.imbue(std::locale::classic());
    logFile << "frame,domain,scope,milliseconds\n" << std::fixed << std::setprecision(6);
    LOG_INFO("Performance log: {}", path.string());
}

void Profiler::stopLogging()
{
    if (!logFile.is_open())
        return;
    logFile.flush();
    CHECK(logFile.good());
    logFile.close();
}

void Profiler::logTiming(uint64_t sourceFrame, std::string_view domain, std::string_view name, double milliseconds)
{
    if (logFile.is_open())
        logFile << sourceFrame << ',' << domain << ',' << std::quoted(std::string{name}, '"', '"') << ','
                << milliseconds << '\n';
}

CpuProfileScope::CpuProfileScope(Profiler* target, std::string_view label) : profiler(target), name(label) {}
CpuProfileScope::~CpuProfileScope()
{
    if (profiler)
        profiler->recordCpu(name, timer.elapsedMilliseconds());
}
