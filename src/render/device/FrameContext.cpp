#include "render/device/FrameContext.h"

#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/DescriptorManager.h"

#include <utility>

void FrameContext::init(const VulkanContext& vulkan, DescriptorManager& descriptors)
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

    imageAvailable = vkCheck(vulkan.deviceHandle().createSemaphore(vk::SemaphoreCreateInfo()));
    // CPU等GPU draw完之后再复用该frame的相关资源，比如UBO等
    drawFence       = vkCheck(
        vulkan.deviceHandle().createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));

    sceneBuffer = Buffer(
        vulkan.physicalDeviceHandle(), vulkan.deviceHandle(),
        sizeof(SceneUniforms), vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
    sceneSet = descriptors.allocate(DescriptorLayoutPreset::Scene);
    const vk::DescriptorBufferInfo bufferInfo{
        .buffer = sceneBuffer.handle(),
        .range = sceneBuffer.size(),
    };
    const vk::WriteDescriptorSet write{
        .dstSet = *sceneSet,
        .dstBinding = RenderInterface::sceneUniformBinding,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &bufferInfo,
    };
    vulkan.deviceHandle().updateDescriptorSets(write, {});
}

void FrameContext::reset() noexcept
{
    sceneSet = nullptr;
    sceneBuffer.reset();
    drawFence             = nullptr;
    imageAvailable        = nullptr;
    graphicsCommandBuffer = nullptr;
    graphicsCommandPool   = nullptr;
}

void FrameContext::updateScene(const SceneUniforms& data)
{
    sceneBuffer.upload(&data, sizeof(data));
}

vk::DescriptorSet FrameContext::sceneSetHandle() const
{
    return *sceneSet;
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
