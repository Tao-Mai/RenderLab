#pragma once

#include "render/resource/Buffer.h"
#include "render/resource/ShaderData.h"

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class DescriptorManager;

class FrameContext
{
public:
    void init(const VulkanContext& vulkan, DescriptorManager& descriptors);
    void reset() noexcept;
    void updateScene(const SceneUniforms& data);
    [[nodiscard]] vk::DescriptorSet sceneSetHandle() const;

    [[nodiscard]] const vk::raii::CommandPool& commandPoolHandle() const;
    [[nodiscard]] vk::raii::CommandBuffer&     commandBufferHandle();
    [[nodiscard]] vk::Semaphore                imageAvailableSemaphore() const;
    [[nodiscard]] vk::Fence                    drawFenceHandle() const;

private:
    vk::raii::CommandPool   graphicsCommandPool   = nullptr;
    vk::raii::CommandBuffer graphicsCommandBuffer = nullptr;
    vk::raii::Semaphore     imageAvailable        = nullptr;
    vk::raii::Fence         drawFence             = nullptr;
    Buffer                   sceneBuffer;
    vk::raii::DescriptorSet sceneSet              = nullptr;
};
