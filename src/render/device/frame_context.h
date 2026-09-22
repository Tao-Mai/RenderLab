#pragma once

#include "render/device/gpu_upload_context.h"

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;

class FrameContext
{
public:
    void init(const VulkanContext& vulkan);
    void reset() noexcept;

    [[nodiscard]] const vk::raii::CommandPool& commandPoolHandle() const;
    [[nodiscard]] vk::raii::CommandBuffer& commandBufferHandle();
    [[nodiscard]] vk::Semaphore imageAvailableSemaphore() const;
    [[nodiscard]] vk::Semaphore renderFinishedSemaphore() const;
    [[nodiscard]] vk::Fence drawFenceHandle() const;
    [[nodiscard]] GpuUploadContext uploadContext(VulkanContext& vulkan) const;

private:
    vk::raii::CommandPool commandPool = nullptr;
    vk::raii::CommandBuffer commandBuffer = nullptr;
    vk::raii::Semaphore presentComplete = nullptr;
    vk::raii::Semaphore renderFinished = nullptr;
    vk::raii::Fence drawFence = nullptr;
};
