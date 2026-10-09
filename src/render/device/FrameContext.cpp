#include "render/device/FrameContext.h"

#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <utility>

void FrameContext::init(VulkanContext& targetVulkan, DescriptorManager& descriptors)
{
    DCHECK(!vulkan);
    vulkan = &targetVulkan;
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
    vulkan                = nullptr;
}

void FrameContext::wait() const
{
    DCHECK(vulkan);
    vkCheck(vulkan->deviceHandle().waitForFences(*drawFence, vk::True, UINT64_MAX));
}

void FrameContext::submit(vk::Semaphore renderFinished, vk::PipelineStageFlags waitStages) const
{
    DCHECK(vulkan);
    vkCheck(vulkan->deviceHandle().resetFences(*drawFence));
    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &*imageAvailable,
        .pWaitDstStageMask    = &waitStages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &*graphicsCommandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &renderFinished,
    };
    vkCheck(vulkan->queueHandle().submit(submitInfo, *drawFence));
}
