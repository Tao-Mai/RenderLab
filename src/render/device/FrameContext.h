#pragma once

#include "render/device/GpuUploadContext.h"

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;

class FrameContext
{
public:
    void init(const VulkanContext& vulkan);
    void reset() noexcept;

    [[nodiscard]] const vk::raii::CommandPool& commandPoolHandle() const;
    [[nodiscard]] vk::raii::CommandBuffer&     commandBufferHandle();
    [[nodiscard]] vk::Semaphore                imageAvailableSemaphore() const;
    [[nodiscard]] vk::Semaphore                renderFinishedSemaphore() const;
    [[nodiscard]] vk::Fence                    drawFenceHandle() const;
    [[nodiscard]] GpuUploadContext             uploadContext(VulkanContext& vulkan) const;

private:
    // 一个线程一个pool
    vk::raii::CommandPool   graphicsCommandPool   = nullptr;
    vk::raii::CommandBuffer graphicsCommandBuffer = nullptr;
    vk::raii::Semaphore     presentComplete       = nullptr;
    vk::raii::Semaphore     renderFinished        = nullptr;
    vk::raii::Fence         drawFence             = nullptr;
};
