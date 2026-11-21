#pragma once

#include "core/profiling/Profiler.h"

#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class VulkanContext;

class GpuProfiler
{
public:
    void     init(const VulkanContext& vulkan);
    void     reset();
    void     discard();
    void     beginFrame(vk::raii::CommandBuffer& commands, Profiler* profiler);
    void     endFrame(vk::raii::CommandBuffer& commands);
    void     collect(Profiler* profiler);
    uint32_t beginScope(vk::raii::CommandBuffer& commands, std::string_view name, bool renderPass = false);
    void     endScope(vk::raii::CommandBuffer& commands, uint32_t scope);

private:
    static constexpr uint32_t kQueriesPerPool = 128;
    struct Scope
    {
        std::string name;
        bool        renderPass;
    };
    void writeTimestamp(vk::raii::CommandBuffer& commands, uint32_t query, vk::PipelineStageFlagBits2 stage);

    const vk::raii::Device*          device = nullptr;
    std::vector<vk::raii::QueryPool> timestampPools;
    uint32_t                         usedPools         = 0;
    uint32_t                         validBits         = 0;
    double                           timestampPeriod   = 0.0;
    bool                             pending           = false;
    Profiler*                        recordingProfiler = nullptr;
    uint64_t                         frameNumber       = 0;
    std::vector<Scope>               scopes;
};

class GpuProfileScope
{
public:
    GpuProfileScope(GpuProfiler* profiler, vk::raii::CommandBuffer& commands, std::string_view name);
    ~GpuProfileScope();
    GpuProfileScope(const GpuProfileScope&)            = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    vk::raii::CommandBuffer& commands;
    GpuProfiler*             profiler;
    uint32_t                 scope = ~0u;
};
