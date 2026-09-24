#include "render/device/FrameContext.h"

#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <utility>

void FrameContext::init(const VulkanContext& vulkan)
{
    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = vulkan.graphicsQueueFamilyIndex(),
    };
    graphicsCommandPool = vkCheck(
        vulkan.deviceHandle().createCommandPool(poolInfo));

    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = *graphicsCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    graphicsCommandBuffer = std::move(vkCheck(
        vulkan.deviceHandle().allocateCommandBuffers(allocationInfo)).front());

    // 等present完再submit
    presentComplete = vkCheck(vulkan.deviceHandle().createSemaphore(vk::SemaphoreCreateInfo()));
    // 等render finish再present
    renderFinished  = vkCheck(vulkan.deviceHandle().createSemaphore(vk::SemaphoreCreateInfo()));
    // CPU等GPU draw完之后再复用该frame的相关资源，比如UBO等
    drawFence       = vkCheck(
        vulkan.deviceHandle().createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
}

void FrameContext::reset() noexcept
{
    drawFence             = nullptr;
    renderFinished        = nullptr;
    presentComplete       = nullptr;
    graphicsCommandBuffer = nullptr;
    graphicsCommandPool   = nullptr;
}

const vk::raii::CommandPool& FrameContext::commandPoolHandle() const
{
    return graphicsCommandPool;
}

vk::raii::CommandBuffer& FrameContext::commandBufferHandle()
{
    return graphicsCommandBuffer;
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
        graphicsCommandPool,
        vulkan.queueHandle(),
    };
}
