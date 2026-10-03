#pragma once

#include "render/FrameData.h"

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class DescriptorManager;

class FrameContext
{
public:
    FrameData data;

    void init(const VulkanContext& vulkan, DescriptorManager& descriptors);
    void reset() noexcept;
    [[nodiscard]] const vk::raii::CommandPool& commandPoolHandle() const;
    [[nodiscard]] vk::raii::CommandBuffer&     commandBufferHandle();
    [[nodiscard]] vk::Semaphore                imageAvailableSemaphore() const;
    [[nodiscard]] vk::Fence                    drawFenceHandle() const;

private:
    vk::raii::CommandPool   graphicsCommandPool   = nullptr;
    vk::raii::CommandBuffer graphicsCommandBuffer = nullptr;
    vk::raii::Semaphore     imageAvailable        = nullptr;
    vk::raii::Fence         drawFence             = nullptr;
};
