#include "render/resources/GpuMesh.h"

#include "render/device/RhiCheck.h"

GpuMesh::GpuMesh(
    rhi::IDevice& device, rhi::ICommandEncoder& commands, const MeshGeometry& geometry,
    std::span<const MeshAsset::Submesh> submeshes
) : vertexTotal(static_cast<uint32_t>(geometry.vertices.size()))
{
    CHECK(
        !geometry.vertices.empty() && !geometry.indices.empty() && !submeshes.empty(),
        "mesh requires geometry and submeshes"
    );
    for (uint32_t index : geometry.indices)
    {
        CHECK(index < geometry.vertices.size(), "mesh index references a missing vertex");
    }
    const auto indexTotal = geometry.indices.size();
    for (const auto& part : submeshes)
    {
        CHECK(
            part.firstIndex <= indexTotal && part.indexCount <= indexTotal - part.firstIndex,
            "mesh submesh range is invalid"
        );
    }


    const auto      rayUsage = device.hasFeature(rhi::Feature::RayTracing)
             ? rhi::BufferUsage::AccelerationStructureBuildInput | rhi::BufferUsage::ShaderResource
             : rhi::BufferUsage::None;
    const auto usage = rhi::BufferUsage::CopySource | rhi::BufferUsage::CopyDestination | rayUsage;
    rhi::BufferDesc desc;
    desc.size  = sizeof(Vertex) * geometry.vertices.size();
    desc.usage = rhi::BufferUsage::VertexBuffer | usage;
    rhiCheck(device.createBuffer(desc, nullptr, vertexBuffer.writeRef()));
    rhiCheck(commands.uploadBufferData(vertexBuffer, 0, desc.size, geometry.vertices.data()));

    desc.size  = sizeof(uint32_t) * geometry.indices.size();
    desc.usage = rhi::BufferUsage::IndexBuffer | usage;
    rhiCheck(device.createBuffer(desc, nullptr, indexBuffer.writeRef()));
    rhiCheck(commands.uploadBufferData(indexBuffer, 0, desc.size, geometry.indices.data()));
}
