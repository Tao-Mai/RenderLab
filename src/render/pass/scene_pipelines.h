#pragma once

#include <vulkan/vulkan_raii.hpp>

class Shader;

class ScenePipelines
{
public:
    void init(
        const vk::raii::Device& device,
        vk::Format colorFormat,
        vk::Format depthFormat,
        vk::DescriptorSetLayout sceneLayout,
        vk::DescriptorSetLayout materialLayout,
        const Shader& sceneShader,
        const Shader& lightShader);
    void reset() noexcept;

    void bindScene(vk::raii::CommandBuffer& commandBuffer) const;
    void bindLightMarkers(vk::raii::CommandBuffer& commandBuffer) const;

    [[nodiscard]] vk::PipelineLayout layoutHandle() const;

private:
    vk::raii::PipelineLayout layout = nullptr;
    vk::raii::Pipeline scenePipeline = nullptr;
    vk::raii::Pipeline lightMarkerPipeline = nullptr;
};
