#pragma once

#include "asset/MeshAsset.h"
#include "asset/MeshGeometry.h"
#include "render/device/GpuUploadContext.h"
#include "render/resources/Buffer.h"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class GpuMesh
{
public:
    GpuMesh(GpuUploadContext upload, MeshGeometry geometry, std::vector<MeshAsset::Submesh> submeshes, bool rayTracing);

    GpuMesh(const GpuMesh&)                = delete;
    GpuMesh& operator=(const GpuMesh&)     = delete;
    GpuMesh(GpuMesh&&) noexcept            = default;
    GpuMesh& operator=(GpuMesh&&) noexcept = default;

    void                                                 bind(vk::raii::CommandBuffer& commandBuffer) const;
    [[nodiscard]] uint32_t                               indexCount() const;
    [[nodiscard]] const std::vector<MeshAsset::Submesh>& submeshes() const { return parts; }
    [[nodiscard]] const Buffer&                          vertices() const { return vertexBuffer; }
    [[nodiscard]] const Buffer&                          indices() const { return indexBuffer; }
    [[nodiscard]] uint32_t                               vertexCount() const { return vertexTotal; }

private:
    uint32_t                        indexTotal  = 0;
    uint32_t                        vertexTotal = 0;
    std::vector<MeshAsset::Submesh> parts;
    Buffer                          vertexBuffer;
    Buffer                          indexBuffer;

    static void copyBuffer(
        const vk::raii::Device&      device,
        const vk::raii::CommandPool& commandPool,
        vk::raii::Queue&             queue,
        const Buffer&                source,
        const Buffer&                destination
    );
};
