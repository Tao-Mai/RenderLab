#pragma once

#include "render/RenderConfig.h"
#include "render/ShaderManager.h"
#include "render/scene/GpuScene.h"

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class PipelineManager;
class VulkanContext;
class FrameContext;

class ShadowPass
{
public:
    void init(const VulkanContext& vulkan, PipelineManager& pipelines,
        ShaderHandle shader, const std::array<FrameContext, maxFramesInFlight>& frames);
    void reset() noexcept;

    void record(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex,
        const Scene::Desc::Object* lightObject,
        const std::vector<SceneRenderItem>& renderItems) const;

private:
    static constexpr uint32_t mapSize = 1024;

    struct Target
    {
        vk::raii::Image image = nullptr;
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::ImageView cubeView = nullptr;
        std::array<vk::raii::ImageView, 6> faceViews{
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    };

    std::array<Target, maxFramesInFlight> targets;
    vk::raii::Sampler sampler = nullptr;
    vk::Format depthFormat = vk::Format::eUndefined;
    vk::Pipeline pipeline = nullptr;
    vk::PipelineLayout pipelineLayout = nullptr;
    const std::array<FrameContext, maxFramesInFlight>* frames = nullptr;

    void transition(vk::raii::CommandBuffer& commandBuffer, vk::Image image,
        vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
        vk::PipelineStageFlags2 srcStages, vk::AccessFlags2 srcAccess,
        vk::PipelineStageFlags2 dstStages, vk::AccessFlags2 dstAccess) const;
};
