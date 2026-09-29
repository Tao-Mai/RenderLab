#pragma once

#include "render/PipelineManager.h"
#include "render/resource/ShaderData.h"

#include <vulkan/vulkan_raii.hpp>

class Swapchain;

class SkyboxPass
{
public:
    void init(Swapchain& swapchain, PipelineManager& pipelines, ShaderHandle shader);
    void refreshPipeline();
    void reset() noexcept;

    void record(vk::raii::CommandBuffer& commandBuffer,
        vk::DescriptorSet sceneSet, const SkyboxPushConstants& parameters) const;

private:
    Swapchain* swapchain = nullptr;
    PipelineManager* pipelines = nullptr;
    ShaderHandle shader;
    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;
};
