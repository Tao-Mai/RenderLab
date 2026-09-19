#pragma once

#include "asset/mesh_data.h"
#include "render/buffer.h"

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
        MeshData meshData);

    Mesh(const Mesh &)            = delete;
    Mesh &operator=(const Mesh &) = delete;
    Mesh(Mesh &&) noexcept        = default;
    Mesh &operator=(Mesh &&) noexcept = default;

    void bind(vk::raii::CommandBuffer &commandBuffer) const;
    [[nodiscard]] uint32_t indexCount() const;
    [[nodiscard]] const std::vector<SubmeshData> &submeshes() const;
    [[nodiscard]] const Material &material(uint32_t index) const;
    [[nodiscard]] std::vector<Material> &materials();

  private:
    MeshData              data;
    Buffer                vertexBuffer;
    Buffer                indexBuffer;

    static void copyBuffer(
        const vk::raii::Device &device,
        const vk::raii::CommandPool &commandPool,
        vk::raii::Queue &queue,
        const Buffer &source,
        const Buffer &destination);
};
