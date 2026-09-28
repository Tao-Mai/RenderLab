#pragma once

#include "asset/MeshGeometry.h"
#include "render/device/GpuUploadContext.h"
#include "render/resource/Buffer.h"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class GpuMaterial;

struct Submesh
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    GpuMaterial* material = nullptr;
};

class GpuMesh
{
public:
    GpuMesh(GpuUploadContext upload, MeshGeometry geometry, std::vector<Submesh> submeshes);

    GpuMesh(const GpuMesh&)            = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&&) noexcept        = default;
    GpuMesh& operator=(GpuMesh&&) noexcept = default;

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
