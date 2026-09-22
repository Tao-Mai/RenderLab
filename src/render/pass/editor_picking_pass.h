#pragma once

#include "render/resource/buffer.h"

#include <cstdint>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class Mesh;
class Shader;

class EditorPickingPass
{
public:
    void init(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device& device,
        vk::Extent2D extent,
        vk::Format depthFormat,
        vk::DescriptorSetLayout sceneLayout,
        const Shader& shader);
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
    vk::Format format = vk::Format::eR32Uint;
    vk::raii::DeviceMemory imageMemory = nullptr;
    vk::raii::Image image = nullptr;
    vk::raii::ImageView imageView = nullptr;
    Buffer readbackBuffer;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;
};
