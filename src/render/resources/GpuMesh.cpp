#include "render/resources/GpuMesh.h"

#include "render/device/RhiCheck.h"

GpuMesh::GpuMesh(
    rhi::IDevice& device, const MeshGeometry& geometry, std::span<const MeshAsset::Submesh> submeshes
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
    rhi::BufferDesc desc;
    desc.size  = sizeof(Vertex) * geometry.vertices.size();
    desc.usage = rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::CopySource | rayUsage;
    rhiCheck(device.createBuffer(desc, geometry.vertices.data(), vertexBuffer.writeRef()));

    desc.size  = sizeof(uint32_t) * geometry.indices.size();
    desc.usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::CopySource | rayUsage;
    rhiCheck(device.createBuffer(desc, geometry.indices.data(), indexBuffer.writeRef()));
}
