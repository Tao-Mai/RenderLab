#pragma once

#include "render/resource/Buffer.h"
#include "render/ShaderManager.h"

#include <cstdint>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class Mesh;
class PipelineManager;
class Swapchain;

class EditorPickingPass
{
public:
    void init(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device& device,
        Swapchain& swapchain,
        PipelineManager& pipelines,
        ShaderHandle shader);
    void refreshPipeline();
    void reset() noexcept;

    void begin(
        vk::raii::CommandBuffer& commandBuffer,
        vk::Image depthImage,
        vk::ImageView depthImageView,
        vk::DescriptorSet sceneDescriptorSet,
        uint32_t x,
        uint32_t y) const;
    void draw(
        vk::raii::CommandBuffer& commandBuffer,
        Mesh& mesh,
        const glm::mat4& model,
        uint32_t selectionId) const;
    void end(vk::raii::CommandBuffer& commandBuffer, uint32_t x, uint32_t y) const;
    [[nodiscard]] uint32_t readSelectionId();
    [[nodiscard]] vk::PipelineLayout layout() const;

private:
    Swapchain* swapchain = nullptr;
    PipelineManager* pipelines = nullptr;
    ShaderHandle shader;
    Buffer readbackBuffer;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;
};
