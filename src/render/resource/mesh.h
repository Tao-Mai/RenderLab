#pragma once

#include "asset/imported_mesh.h"
#include "render/device/gpu_upload_context.h"
#include "render/resource/buffer.h"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class Material;

struct Submesh
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    Material* material = nullptr;
};

class Mesh
{
public:
    Mesh(GpuUploadContext upload, MeshGeometry geometry, std::vector<Submesh> submeshes);

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) noexcept        = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    void bind(vk::raii::CommandBuffer& commandBuffer) const;
    [[nodiscard]] uint32_t indexCount() const;
    [[nodiscard]] const std::vector<Submesh>& submeshes() const;

private:
    uint32_t indexTotal = 0;
    std::vector<Submesh> parts;
    Buffer vertexBuffer;
    Buffer indexBuffer;

    static void copyBuffer(
        const vk::raii::Device&      device,
        const vk::raii::CommandPool& commandPool,
        vk::raii::Queue&             queue,
        const Buffer&                source,
        const Buffer&                destination);
};
