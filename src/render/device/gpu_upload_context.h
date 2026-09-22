#pragma once

#include <vulkan/vulkan_raii.hpp>

struct GpuUploadContext
{
    const vk::raii::PhysicalDevice& physicalDevice;
    const vk::raii::Device& device;
    const vk::raii::CommandPool& commandPool;
    vk::raii::Queue& queue;
};
