#pragma once

#include "core/profiling/Profiler.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class CommandRecorder;

class GpuProfiler
{
public:
    void init(const VulkanContext& vulkan);
    void reset();
    void discard();
    void beginFrame(CommandRecorder& commands, std::span<const std::string_view> names, Profiler* profiler);
    void beginPass(CommandRecorder& commands, uint32_t index);
    void endPass(CommandRecorder& commands, uint32_t index) const;
    void endFrame(CommandRecorder& commands);
    void collect(Profiler* profiler);
    uint32_t beginScope(CommandRecorder& commands, std::string_view name);
    void endScope(CommandRecorder& commands, uint32_t scope) const;

private:
    static constexpr uint32_t kMaxScopes = 64;
    struct Scope
    {
        std::string name;
        uint32_t begin;
        uint32_t end;
    };
    const vk::raii::Device*         device          = nullptr;
    vk::raii::QueryPool             pool            = nullptr;
    uint32_t                        capacity        = 0;
    uint32_t                        validBits       = 0;
    double                          timestampPeriod = 0.0;
    bool                            pending         = false;
    std::vector<std::string>        passNames;
    std::vector<PassDrawStatistics> passStatistics;
    Profiler*                       recordingProfiler = nullptr;
    uint64_t frameNumber = 0;
    std::vector<Scope> scopes;
};

class GpuProfileScope
{
public:
    GpuProfileScope(CommandRecorder& commands, std::string_view name);
    ~GpuProfileScope();
    GpuProfileScope(const GpuProfileScope&) = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    CommandRecorder& commands;
    GpuProfiler* profiler;
    uint32_t scope = ~0u;
};
