#pragma once

#include "render/resource/Buffer.h"
#include "render/resource/ShaderData.h"

#include <span>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class DescriptorManager;

class FrameContext
{
public:
    void init(const VulkanContext& vulkan, DescriptorManager& descriptors);
    void reset() noexcept;
    // Call after this frame's fence completes, before recording/submitting.
    void updateFrameData(const ViewUniforms& view, const LightUniforms& lighting,
        std::span<const LightData> lights);
    [[nodiscard]] const ViewUniforms& viewUniforms() const;
    [[nodiscard]] const LightUniforms& lightUniforms() const;
    [[nodiscard]] std::span<const LightData> lightData() const;
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
    const VulkanContext* vulkan = nullptr;
    ViewUniforms viewData;
    LightUniforms lightMetadata;
    std::vector<LightData> lights;
    Buffer viewBuffer;
    Buffer lightUniformBuffer;
    Buffer lightBuffer;
    vk::raii::DescriptorSet sceneSet              = nullptr;

    void bindFrameBuffers();
};
