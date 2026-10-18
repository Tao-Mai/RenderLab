#pragma once

#include "core/time/Timer.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ProfileTiming
{
    std::string name;
    double      milliseconds = 0.0;
};

struct DrawStatistics
{
    uint64_t drawCalls = 0;
    uint64_t triangles = 0;
};

struct PassDrawStatistics
{
    std::string    name;
    DrawStatistics draws;
};

struct ViewCullingStatistics
{
    std::string name;
    uint32_t    visibleObjects = 0;
};

struct PerformanceSnapshot
{
    double                             frameMilliseconds = 0.0;
    double                             cpuMilliseconds   = 0.0;
    double                             gpuMilliseconds   = 0.0;
    double                             p95Milliseconds   = 0.0;
    double                             p99Milliseconds   = 0.0;
    bool                               hasGpuTimings     = false;
    uint32_t                           totalObjects      = 0;
    std::vector<float>                 frameHistory;
    std::vector<ProfileTiming>         cpuTimings;
    std::vector<ProfileTiming>         gpuTimings;
    std::vector<PassDrawStatistics>    passes;
    std::vector<ViewCullingStatistics> views;
};

class Profiler
{
public:
    void beginFrame();
    void endFrame(double frameMilliseconds);
    void reset();
    void startLogging(const std::filesystem::path& path);
    void stopLogging();
    void recordCpu(std::string_view name, double milliseconds);
    void recordGpu(uint64_t frame, double milliseconds, std::vector<ProfileTiming> timings);
    void recordDraws(std::vector<PassDrawStatistics> passes);
    void recordCulling(uint32_t totalObjects, std::vector<ViewCullingStatistics> views);
    void clearGpu();
    [[nodiscard]] const PerformanceSnapshot&        display() const { return displayed; }
    [[nodiscard]] const std::vector<ProfileTiming>& gpuTimings() const { return current.gpuTimings; }
    [[nodiscard]] uint64_t frameNumber() const { return frame; }

private:
    static constexpr size_t kHistorySize         = 240;
    static constexpr double kRefreshMilliseconds = 500.0;
    Timer                   frameTimer;
    Timer                   refreshTimer;
    bool                    capturing = false;
    uint64_t                frame = 0;
    std::ofstream           logFile;
    std::deque<float>       frameHistory;
    std::deque<double>      cpuHistory;
    PerformanceSnapshot     current;
    PerformanceSnapshot     displayed;
    void logTiming(uint64_t frame, std::string_view domain, std::string_view name, double milliseconds);
};

class CpuProfileScope
{
public:
    CpuProfileScope(Profiler* profiler, std::string_view name);
    ~CpuProfileScope();
    CpuProfileScope(const CpuProfileScope&)            = delete;
    CpuProfileScope& operator=(const CpuProfileScope&) = delete;

private:
    Profiler*        profiler;
    std::string_view name;
    Timer            timer;
};
