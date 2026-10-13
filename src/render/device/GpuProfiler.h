#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class Profiler;

class GpuProfiler
{
public:
    void init(const VulkanContext& vulkan);
    void reset();
    void discard();
    void beginFrame(vk::raii::CommandBuffer& commands, std::span<const std::string_view> names);
    void beginPass(vk::raii::CommandBuffer& commands, uint32_t index) const;
    void endPass(vk::raii::CommandBuffer& commands, uint32_t index) const;
    void endFrame(vk::raii::CommandBuffer& commands);
    void collect(Profiler* profiler);

private:
    const vk::raii::Device*  device          = nullptr;
    vk::raii::QueryPool      pool            = nullptr;
    uint32_t                 capacity        = 0;
    uint32_t                 validBits       = 0;
    double                   timestampPeriod = 0.0;
    bool                     pending         = false;
    std::vector<std::string> passNames;
};
