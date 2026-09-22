#pragma once

#include <vulkan/vulkan_raii.hpp>

class ScenePipelines
{
public:
    void initialize(
        const vk::raii::Device& device,
        vk::Format colorFormat,
        vk::Format depthFormat,
        vk::DescriptorSetLayout sceneLayout,
        vk::DescriptorSetLayout materialLayout);
    void reset() noexcept;

    void bindScene(vk::raii::CommandBuffer& commandBuffer) const;
    void bindLightMarkers(vk::raii::CommandBuffer& commandBuffer) const;

    [[nodiscard]] vk::PipelineLayout layoutHandle() const;

private:
    vk::raii::PipelineLayout layout = nullptr;
    vk::raii::Pipeline scenePipeline = nullptr;
    vk::raii::Pipeline lightMarkerPipeline = nullptr;
};
