#include "render/device/FrameContext.h"

#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/DescriptorManager.h"

#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <utility>

void FrameContext::init(const VulkanContext& targetVulkan, DescriptorManager& descriptors)
{
    vulkan = &targetVulkan;
    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = vulkan->graphicsQueueFamilyIndex(),
    };
    graphicsCommandPool = vkCheck(
        vulkan->deviceHandle().createCommandPool(poolInfo));

    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = *graphicsCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    graphicsCommandBuffer = std::move(vkCheck(
        vulkan->deviceHandle().allocateCommandBuffers(allocationInfo)).front());

    imageAvailable = vkCheck(vulkan->deviceHandle().createSemaphore(vk::SemaphoreCreateInfo()));
    // CPU等GPU draw完之后再复用该frame的相关资源，比如UBO等
    drawFence       = vkCheck(
        vulkan->deviceHandle().createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));

    const auto hostMemory = vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent;
    viewBuffer = Buffer(vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
        sizeof(ViewUniforms), vk::BufferUsageFlagBits::eUniformBuffer, hostMemory);
    lightUniformBuffer = Buffer(vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
        sizeof(LightUniforms), vk::BufferUsageFlagBits::eUniformBuffer, hostMemory);
    // Even a scene without lights needs a nonempty, valid storage descriptor.
    lightBuffer = Buffer(vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
        sizeof(LightData), vk::BufferUsageFlagBits::eStorageBuffer, hostMemory);
    sceneSet = descriptors.allocate(DescriptorLayoutPreset::Scene);
    bindFrameBuffers();
}

void FrameContext::bindFrameBuffers()
{
    const std::array infos = {
        vk::DescriptorBufferInfo{.buffer = viewBuffer.handle(), .range = viewBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = lightUniformBuffer.handle(), .range = lightUniformBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = lightBuffer.handle(), .range = lightBuffer.size()},
    };
    const std::array writes = {
        vk::WriteDescriptorSet{.dstSet = *sceneSet, .dstBinding = RenderInterface::viewUniformBinding,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &infos[0]},
        vk::WriteDescriptorSet{.dstSet = *sceneSet, .dstBinding = RenderInterface::lightUniformBinding,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &infos[1]},
        vk::WriteDescriptorSet{.dstSet = *sceneSet, .dstBinding = RenderInterface::lightBufferBinding,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo = &infos[2]},
    };
    vulkan->deviceHandle().updateDescriptorSets(writes, {});
}

void FrameContext::reset() noexcept
{
    sceneSet = nullptr;
    viewBuffer.reset();
    lightUniformBuffer.reset();
    lightBuffer.reset();
    viewData = {};
    lightMetadata = {};
    lights.clear();
    vulkan = nullptr;
    drawFence             = nullptr;
    imageAvailable        = nullptr;
    graphicsCommandBuffer = nullptr;
    graphicsCommandPool   = nullptr;
}

void FrameContext::updateFrameData(const ViewUniforms& view, const LightUniforms& lighting,
    std::span<const LightData> frameLights)
{
    CHECK(vulkan != nullptr && lighting.lightCount == frameLights.size(),
        "frame light count must match the uploaded SSBO elements");
    const vk::DeviceSize bytes = std::max<size_t>(frameLights.size(), 1) * sizeof(LightData);
    CHECK(bytes <= vulkan->properties().limits.maxStorageBufferRange,
        "light SSBO exceeds maxStorageBufferRange");

    if (bytes > lightBuffer.size())
    {
        // The current frame's fence is complete; other frames keep their buffers.
        lightBuffer.reset();
        lightBuffer = Buffer(vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
            bytes, vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        bindFrameBuffers();
    }

    viewData = view;
    lightMetadata = lighting;
    lights.assign(frameLights.begin(), frameLights.end());
    viewBuffer.upload(&viewData, sizeof(viewData));
    lightUniformBuffer.upload(&lightMetadata, sizeof(lightMetadata));
    if (!lights.empty()) lightBuffer.upload(lights.data(), lights.size() * sizeof(LightData));
    // Host-coherent writes become available to device reads at queue submission.
}

const ViewUniforms& FrameContext::viewUniforms() const { return viewData; }
const LightUniforms& FrameContext::lightUniforms() const { return lightMetadata; }
std::span<const LightData> FrameContext::lightData() const { return lights; }

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
