#include "render/resources/GpuMesh.h"

#include "render/RhiUtils.h"

GpuMesh::GpuMesh(
    rhi::IDevice& device, rhi::ICommandEncoder& commands, const MeshGeometry& geometry,
    std::span<const MeshAsset::Submesh> submeshes
) : vertexTotal(static_cast<uint32_t>(geometry.vertices.size())),
    indexTotal(static_cast<uint32_t>(geometry.indices.size()))
{
    const auto kBufferUsage = 
        rhi::BufferUsage::CopySource | 
        rhi::BufferUsage::CopyDestination |
        rhi::BufferUsage::AccelerationStructureBuildInput | 
        rhi::BufferUsage::ShaderResource;

    rhi::BufferDesc desc;

    // Vertex Buffer
    desc.size  = sizeof(Vertex) * geometry.vertices.size();
    desc.usage = rhi::BufferUsage::VertexBuffer | kBufferUsage;
    
    rhiCheck(device.createBuffer(desc, nullptr, vertexBuffer.writeRef()));
    rhiCheck(commands.uploadBufferData(vertexBuffer, 0, desc.size, geometry.vertices.data()));

    // Index Buffer
    desc.size  = sizeof(uint32_t) * geometry.indices.size();
    desc.usage = rhi::BufferUsage::IndexBuffer | kBufferUsage;
    
    rhiCheck(device.createBuffer(desc, nullptr, indexBuffer.writeRef()));
    rhiCheck(commands.uploadBufferData(indexBuffer, 0, desc.size, geometry.indices.data()));
}
