#pragma once

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
namespace Falcor
{
class Buffer;
}

struct GpuUploadContext
{
    VulkanContext& vulkan;
    const vk::raii::CommandPool& commandPool;

    void copyBuffer(const Falcor::Buffer& source, const Falcor::Buffer& destination, const vk::BufferCopy& region) const;
};
