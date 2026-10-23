#include "render/device/FrameContext.h"

#include "core/Context.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <utility>

void FrameContext::init(VulkanContext& targetVulkan)
{
    DCHECK(!vulkan);
    vulkan = &targetVulkan;
    const vk::CommandPoolCreateInfo poolInfo{
        .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = targetVulkan.graphicsQueueFamilyIndex(),
    };
    graphicsCommandPool = vkCheck(targetVulkan.deviceHandle().createCommandPool(poolInfo));

    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool        = *graphicsCommandPool,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    graphicsCommandBuffer =
        std::move(vkCheck(targetVulkan.deviceHandle().allocateCommandBuffers(allocationInfo)).front());

    imageAvailable = vkCheck(targetVulkan.deviceHandle().createSemaphore(vk::SemaphoreCreateInfo()));
    // CPU等GPU draw完之后再复用该frame的相关资源，比如UBO等
    drawFence = vkCheck(targetVulkan.deviceHandle().createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
    profiling.init(targetVulkan);
}

void FrameContext::reset() noexcept
{
    profiling.reset();

    drawFence             = nullptr;
    imageAvailable        = nullptr;
    graphicsCommandBuffer = nullptr;
    graphicsCommandPool   = nullptr;
    vulkan                = nullptr;
}

void FrameContext::wait()
{
    DCHECK(vulkan);
    vkCheck(vulkan->deviceHandle().waitForFences(*drawFence, vk::True, UINT64_MAX));
    profiling.collect(context().profiler);
}

void FrameContext::submit(vk::Semaphore renderFinished, vk::PipelineStageFlags2 waitStages) const
{
    DCHECK(vulkan);
    vkCheck(vulkan->deviceHandle().resetFences(*drawFence));
    const vk::SemaphoreSubmitInfo wait{
        .semaphore = *imageAvailable,
        .stageMask = waitStages,
    };
    const vk::CommandBufferSubmitInfo commands{.commandBuffer = *graphicsCommandBuffer};
    const vk::SemaphoreSubmitInfo     signal{
            .semaphore = renderFinished,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
    };
    const vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &wait,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &commands,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal,
    };
    vkCheck(vulkan->queueHandle().submit2(submitInfo, *drawFence));
}
