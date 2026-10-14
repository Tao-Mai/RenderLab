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

private:
    const vk::raii::Device*         device          = nullptr;
    vk::raii::QueryPool             pool            = nullptr;
    uint32_t                        capacity        = 0;
    uint32_t                        validBits       = 0;
    double                          timestampPeriod = 0.0;
    bool                            pending         = false;
    std::vector<std::string>        passNames;
    std::vector<PassDrawStatistics> passStatistics;
    Profiler*                       recordingProfiler = nullptr;
};
