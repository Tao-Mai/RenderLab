#pragma once


#include <vulkan/vulkan_raii.hpp>
#include "render/device/CommandRecorder.h"
#include "render/device/GpuProfiler.h"

class VulkanContext;

class FrameContext
{
public:
    void init(VulkanContext& vulkan);
    void reset() noexcept;
    void wait();
    void submit(vk::Semaphore renderFinished, vk::PipelineStageFlags waitStages) const;
    [[nodiscard]] const vk::raii::CommandPool& commandPoolHandle() const { return graphicsCommandPool; }
    [[nodiscard]] CommandRecorder&             commandBufferHandle() { return graphicsCommandBuffer; }
    [[nodiscard]] vk::Semaphore                imageAvailableSemaphore() const { return *imageAvailable; }
    [[nodiscard]] vk::Fence                    drawFenceHandle() const { return *drawFence; }
    [[nodiscard]] GpuProfiler&                 gpuProfiler() { return profiling; }

private:
    VulkanContext*        vulkan                = nullptr;
    vk::raii::CommandPool graphicsCommandPool   = nullptr;
    CommandRecorder       graphicsCommandBuffer = nullptr;
    vk::raii::Semaphore   imageAvailable        = nullptr;
    vk::raii::Fence       drawFence             = nullptr;
    GpuProfiler           profiling;
};
