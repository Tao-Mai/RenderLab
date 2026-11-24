#pragma once

#include "core/profiling/Profiler.h"

#include <slang-rhi.h>
#include <string>
#include <string_view>
#include <vector>

class GpuProfiler
{
public:
    void     init(rhi::IDevice& device);
    void     reset();
    void     discard();
    void     beginFrame(rhi::ICommandEncoder& commands, Profiler* profiler);
    void     endFrame(rhi::ICommandEncoder& commands);
    void     collect(Profiler* profiler);
    uint32_t beginScope(rhi::ICommandEncoder& commands, std::string_view name, bool renderPass = false);
    void     endScope(rhi::ICommandEncoder& commands, uint32_t scope);

private:
    static constexpr uint32_t kQueriesPerPool = 128;
    struct Scope
    {
        std::string name;
        bool        renderPass;
    };
    void writeTimestamp(rhi::ICommandEncoder& commands, uint32_t query);

    rhi::IDevice*                             device = nullptr;
    std::vector<rhi::ComPtr<rhi::IQueryPool>> timestampPools;
    uint32_t                                  usedPools         = 0;
    uint32_t                                  validBits         = 0;
    double                                    timestampPeriod   = 0.0;
    bool                                      pending           = false;
    Profiler*                                 recordingProfiler = nullptr;
    uint64_t                                  frameNumber       = 0;
    std::vector<Scope>                        scopes;
};

class GpuProfileScope
{
public:
    GpuProfileScope(GpuProfiler* profiler, rhi::ICommandEncoder& commands, std::string_view name);
    ~GpuProfileScope();
    GpuProfileScope(const GpuProfileScope&)            = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    rhi::ICommandEncoder& commands;
    GpuProfiler*          profiler;
    uint32_t              scope = ~0u;
};
