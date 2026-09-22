#pragma once

#include "asset/mesh_data.h"
#include "render/device/gpu_upload_context.h"
#include "render/resource/buffer.h"
#include "render/resource/material_gpu.h"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class Mesh
{
public:
    Mesh(GpuUploadContext upload, MeshData meshData);

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) noexcept        = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    void bind(vk::raii::CommandBuffer& commandBuffer) const;
    [[nodiscard]] uint32_t                            indexCount() const;
    [[nodiscard]] const std::vector<SubmeshData>&     submeshes() const;
    [[nodiscard]] const MaterialData&                 materialData(uint32_t index) const;
    [[nodiscard]] const MaterialGpu&                  materialGpu(uint32_t index) const;
    [[nodiscard]] std::vector<MaterialGpu>&           materialGpus();
    [[nodiscard]] const std::vector<MaterialData>&    materials() const;

private:
    MeshData                 data;
    std::vector<MaterialGpu> gpus;
    Buffer                   vertexBuffer;
    Buffer                   indexBuffer;

    static void copyBuffer(
        const vk::raii::Device&      device,
        const vk::raii::CommandPool& commandPool,
        vk::raii::Queue&             queue,
        const Buffer&                source,
        const Buffer&                destination);
};
