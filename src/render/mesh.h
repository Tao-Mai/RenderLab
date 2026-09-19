#pragma once

#include "render/buffer.h"
#include "render/vertex.h"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class Mesh
{
  public:
    Mesh(
        const vk::raii::PhysicalDevice &physicalDevice,
        const vk::raii::Device &device,
        const vk::raii::CommandPool &commandPool,
        vk::raii::Queue &queue,
        std::vector<Vertex> vertices,
        std::vector<uint32_t> indices);

    Mesh(const Mesh &)            = delete;
    Mesh &operator=(const Mesh &) = delete;
    Mesh(Mesh &&) noexcept        = default;
    Mesh &operator=(Mesh &&) noexcept = default;

    void bind(vk::raii::CommandBuffer &commandBuffer) const;
    [[nodiscard]] uint32_t indexCount() const;

  private:
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    Buffer                vertexBuffer;
    Buffer                indexBuffer;

    static void copyBuffer(
        const vk::raii::Device &device,
        const vk::raii::CommandPool &commandPool,
        vk::raii::Queue &queue,
        const Buffer &source,
        const Buffer &destination);
};
