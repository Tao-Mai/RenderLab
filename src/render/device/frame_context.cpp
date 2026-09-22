#include "render/device/frame_context.h"

#include "render/device/vulkan_context.h"

#include <utility>

void FrameContext::init(const VulkanContext& vulkan)
{
    try
    {
        const vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = vulkan.queueFamilyIndex(),
        };
        commandPool = vk::raii::CommandPool(vulkan.deviceHandle(), poolInfo);

        const vk::CommandBufferAllocateInfo allocationInfo{
            .commandPool = *commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };
        commandBuffer = std::move(
            vk::raii::CommandBuffers(vulkan.deviceHandle(), allocationInfo).front());

        presentComplete = vk::raii::Semaphore(
            vulkan.deviceHandle(), vk::SemaphoreCreateInfo());
        renderFinished = vk::raii::Semaphore(
            vulkan.deviceHandle(), vk::SemaphoreCreateInfo());
        drawFence = vk::raii::Fence(
            vulkan.deviceHandle(),
            {.flags = vk::FenceCreateFlagBits::eSignaled});
    }
    catch (...)
    {
        reset();
        throw;
    }
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
