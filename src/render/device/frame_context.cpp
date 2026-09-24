#include "render/device/frame_context.h"

#include "render/device/vk_check.h"
#include "render/device/vulkan_context.h"

#include <utility>

void FrameContext::init(const VulkanContext& vulkan)
{
    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = vulkan.queueFamilyIndex(),
    };
    commandPool = vkCheck(
        vulkan.deviceHandle().createCommandPool(poolInfo));

    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = *commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    commandBuffer = std::move(vkCheck(
        vulkan.deviceHandle().allocateCommandBuffers(allocationInfo)).front());

    presentComplete = vkCheck(
        vulkan.deviceHandle().createSemaphore(vk::SemaphoreCreateInfo()));
    renderFinished = vkCheck(
        vulkan.deviceHandle().createSemaphore(vk::SemaphoreCreateInfo()));
    drawFence = vkCheck(
        vulkan.deviceHandle().createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
}

void FrameContext::reset() noexcept
{
    drawFence = nullptr;
    renderFinished = nullptr;
    presentComplete = nullptr;
    commandBuffer = nullptr;
    commandPool = nullptr;
}

const vk::raii::CommandPool& FrameContext::commandPoolHandle() const
{
    return commandPool;
}

vk::raii::CommandBuffer& FrameContext::commandBufferHandle()
{
    return commandBuffer;
}

vk::Semaphore FrameContext::imageAvailableSemaphore() const
{
    return *presentComplete;
}

vk::Semaphore FrameContext::renderFinishedSemaphore() const
{
    return *renderFinished;
}

vk::Fence FrameContext::drawFenceHandle() const
{
    return *drawFence;
}

GpuUploadContext FrameContext::uploadContext(VulkanContext& vulkan) const
{
    return {
        vulkan.physicalDeviceHandle(),
        vulkan.deviceHandle(),
        commandPool,
        vulkan.queueHandle(),
    };
}
