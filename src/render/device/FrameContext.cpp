#include "render/device/FrameContext.h"

#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <utility>

void FrameContext::init(const VulkanContext& targetVulkan, DescriptorManager& descriptors)
{
    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = targetVulkan.graphicsQueueFamilyIndex(),
    };
    graphicsCommandPool = vkCheck(
        targetVulkan.deviceHandle().createCommandPool(poolInfo));

    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = *graphicsCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    graphicsCommandBuffer = std::move(vkCheck(
        targetVulkan.deviceHandle().allocateCommandBuffers(allocationInfo)).front());

    imageAvailable = vkCheck(targetVulkan.deviceHandle().createSemaphore(vk::SemaphoreCreateInfo()));
    // CPU等GPU draw完之后再复用该frame的相关资源，比如UBO等
    drawFence       = vkCheck(
        targetVulkan.deviceHandle().createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));

    data.init(targetVulkan, descriptors);
}

void FrameContext::reset() noexcept
{
    data.reset();

    drawFence = nullptr;
    imageAvailable = nullptr;
    graphicsCommandBuffer = nullptr;
    graphicsCommandPool = nullptr;
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
    return *imageAvailable;
}

vk::Fence FrameContext::drawFenceHandle() const
{
    return *drawFence;
}
