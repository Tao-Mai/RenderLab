#include "render/resources/GpuMesh.h"

#include <array>
#include <utility>
#include "core/Logger.h"

GpuMesh::GpuMesh(
    GpuUploadContext upload, MeshGeometry geometry, std::vector<MeshAsset::Submesh> submeshes, bool rayTracing
) :
    indexTotal(static_cast<uint32_t>(geometry.indices.size())),
    vertexTotal(static_cast<uint32_t>(geometry.vertices.size())),
    parts(std::move(submeshes))
{
    CHECK(
        !geometry.vertices.empty() && !geometry.indices.empty() && !parts.empty(),
        "mesh requires geometry and submeshes"
    );
    for (uint32_t index : geometry.indices)
    {
        CHECK(index < geometry.vertices.size(), "mesh index references a missing vertex");
    }
    for (const MeshAsset::Submesh& part : parts)
    {
        CHECK(
            part.firstIndex <= indexTotal && part.indexCount <= indexTotal - part.firstIndex,
            "mesh submesh range is invalid"
        );
    }

    auto&                      vulkan         = upload.vulkan;
    const vk::BufferUsageFlags rayUsage       = rayTracing ? vk::BufferUsageFlagBits::eShaderDeviceAddress |
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                                                           : vk::BufferUsageFlags{};

    const vk::DeviceSize vertexBytes = sizeof(Vertex) * geometry.vertices.size();
    Falcor::Buffer               vertexStaging(
        vulkan,
        vertexBytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        Falcor::MemoryType::Upload,
        Falcor::ResourceBindFlags::None
    );
    vertexStaging.setBlob(geometry.vertices.data(), 0, vertexBytes);
    vertexBuffer = Falcor::Buffer(
        vulkan,
        vertexBytes,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer | rayUsage,
        Falcor::MemoryType::DeviceLocal,
        Falcor::ResourceBindFlags::Vertex
    );
    upload.copyBuffer(vertexStaging, vertexBuffer, {.size = vertexBytes});

    const vk::DeviceSize indexBytes = sizeof(uint32_t) * geometry.indices.size();
    Falcor::Buffer               indexStaging(
        vulkan,
        indexBytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        Falcor::MemoryType::Upload,
        Falcor::ResourceBindFlags::None
    );
    indexStaging.setBlob(geometry.indices.data(), 0, indexBytes);
    indexBuffer = Falcor::Buffer(
        vulkan,
        indexBytes,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer | rayUsage,
        Falcor::MemoryType::DeviceLocal,
        Falcor::ResourceBindFlags::Index
    );
    upload.copyBuffer(indexStaging, indexBuffer, {.size = indexBytes});
}

void GpuMesh::bind(vk::raii::CommandBuffer& commandBuffer) const
{
    const std::array<vk::Buffer, 1>     vertexBuffers{vertexBuffer.handle()};
    const std::array<vk::DeviceSize, 1> vertexOffsets{0};
    commandBuffer.bindVertexBuffers(0, vertexBuffers, vertexOffsets);
    commandBuffer.bindIndexBuffer(indexBuffer.handle(), 0, vk::IndexType::eUint32);
}
