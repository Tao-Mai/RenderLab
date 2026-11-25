#include "render/resources/SceneAccelerationStructures.h"

#include <slang-rhi.h>
#include "core/Logger.h"
#include "core/Vertex.h"
#include "render/GpuScene.h"
#include "render/RhiUtils.h"
#include "render/resources/GpuMesh.h"

#include <cstring>

namespace
{
// Match the scene mask in the ray tracing shaders.
constexpr uint32_t kSceneMask          = 0x01;
constexpr uint32_t kInstanceIndexLimit = 1u << 24;

rhi::AccelerationStructureBuildInput triangleGeometry(
    rhi::IBuffer* vertices,
    uint32_t      stride,
    uint32_t      vertexCount,
    rhi::IBuffer* indices,
    uint64_t      indexOffset,
    uint32_t      indexCount
)
{
    rhi::AccelerationStructureBuildInput input{};
    input.type                        = rhi::AccelerationStructureBuildInputType::Triangles;
    input.triangles                   = {};
    input.triangles.vertexBuffers[0]  = vertices;
    input.triangles.vertexBufferCount = 1;
    input.triangles.vertexFormat      = rhi::Format::RGB32Float;
    input.triangles.vertexCount       = vertexCount;
    input.triangles.vertexStride      = stride;
    input.triangles.indexBuffer       = {indices, indexOffset};
    input.triangles.indexFormat       = rhi::IndexFormat::Uint32;
    input.triangles.indexCount        = indexCount;
    input.triangles.flags             = rhi::AccelerationStructureGeometryFlags::Opaque;
    return input;
}

void setInstanceTransform(rhi::AccelerationStructureInstanceDescGeneric& instance, const glm::mat4& model)
{
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            instance.transform[row][column] = model[column][row];
}

std::vector<rhi::AccelerationStructureInstanceDescGeneric> makeInstances(
    std::span<const GpuDraw>                          draws,
    std::span<const rhi::AccelerationStructureHandle> meshBlasAddresses
)
{
    CHECK(draws.size() < kInstanceIndexLimit, "ray tracing instance count exceeds the custom-index limit");

    std::vector<rhi::AccelerationStructureInstanceDescGeneric> instances;
    instances.reserve(draws.size());
    for (const auto& draw : draws)
    {
        const auto                                    index = instances.size();
        rhi::AccelerationStructureInstanceDescGeneric instance{};
        setInstanceTransform(instance, draw.model);
        instance.instanceID            = static_cast<uint32_t>(index);
        instance.instanceMask          = kSceneMask;
        instance.flags                 = rhi::AccelerationStructureInstanceFlags::TriangleFacingCullDisable;
        instance.accelerationStructure = meshBlasAddresses[index];
        instances.push_back(instance);
    }

    return instances;
}

}  // namespace

rhi::IAccelerationStructure* SceneAccelerationStructures::getOrBuild(
    rhi::IDevice& device,
    rhi::ICommandEncoder& commands,
    std::span<const GpuDraw> draws
)
{
    if (builtRevision == revision)
        return tlas;

    const auto meshBlasAddresses = recordBlasBuilds(device, commands, draws);
    const auto instances = makeInstances(draws, meshBlasAddresses);

    // A submitted command buffer retains its TLAS. Build a new one without modifying the old one.
    tlas = recordTlasBuild(device, commands, instances);
    commands.globalBarrier();
    builtRevision = revision;
    return tlas;
}

std::vector<rhi::AccelerationStructureHandle> SceneAccelerationStructures::recordBlasBuilds(
    rhi::IDevice& device, rhi::ICommandEncoder& commands, std::span<const GpuDraw> draws
)
{
    std::vector<rhi::AccelerationStructureHandle> addresses;
    addresses.reserve(draws.size());

    bool builtBlas = false;
    for (const auto& draw : draws)
    {
        const auto& mesh = *draw.mesh;
        const auto [entry, created] = meshBlasCache.try_emplace({&mesh, draw.firstIndex, draw.indexCount});
        auto& blas                  = entry->second;
        if (created)
        {
            CHECK(
                draw.indexCount % 3 == 0,
                "mesh '{}' has {} indices for ray tracing at index {}",
                draw.asset->id,
                draw.indexCount,
                draw.firstIndex
            );

            const auto geometry = triangleGeometry(
                mesh.vertices(),
                sizeof(Vertex),
                mesh.vertexCount(),
                mesh.indices(),
                draw.firstIndex * sizeof(uint32_t),
                draw.indexCount
            );
            rhi::AccelerationStructureBuildDesc build;
            build.inputs     = &geometry;
            build.inputCount = 1;
            build.flags      = rhi::AccelerationStructureBuildFlags::PreferFastTrace;
            rhi::AccelerationStructureSizes sizes;
            rhiCheck(device.getAccelerationStructureSizes(build, &sizes));
            rhi::AccelerationStructureDesc desc;
            desc.kind = rhi::AccelerationStructureKind::BottomLevel;
            desc.size = sizes.accelerationStructureSize;
            rhiCheck(device.createAccelerationStructure(desc, blas.writeRef()));

            rhi::ComPtr<rhi::IBuffer> scratch;
            rhi::BufferDesc scratchDesc;
            scratchDesc.size  = sizes.scratchSize;
            scratchDesc.usage = rhi::BufferUsage::UnorderedAccess;
            rhiCheck(device.createBuffer(scratchDesc, nullptr, scratch.writeRef()));
            commands.buildAccelerationStructure(build, blas, nullptr, scratch, 0, nullptr);

            builtBlas = true;
        }
        addresses.push_back(blas->getHandle());
    }

    if (builtBlas)
        commands.globalBarrier();

    return addresses;
}

rhi::ComPtr<rhi::IAccelerationStructure> SceneAccelerationStructures::recordTlasBuild(
    rhi::IDevice&                                                  device,
    rhi::ICommandEncoder&                                          commands,
    std::span<const rhi::AccelerationStructureInstanceDescGeneric> instances
)
{
    // RHI requires at least one instance. A zero reference is an inactive instance for an empty scene.
    const rhi::AccelerationStructureInstanceDescGeneric inactive{};
    if (instances.empty())
        instances = {&inactive, 1};

    rhi::ComPtr<rhi::IBuffer> instanceBuffer;
    rhi::BufferDesc instanceDesc;
    instanceDesc.size       = instances.size_bytes();
    instanceDesc.memoryType = rhi::MemoryType::Upload;
    instanceDesc.usage      = rhi::BufferUsage::AccelerationStructureBuildInput;
    rhiCheck(device.createBuffer(instanceDesc, nullptr, instanceBuffer.writeRef()));

    void* data;
    rhiCheck(device.mapBuffer(instanceBuffer, rhi::CpuAccessMode::Write, &data));
    std::memcpy(data, instances.data(), instances.size_bytes());
    rhiCheck(device.unmapBuffer(instanceBuffer));

    rhi::AccelerationStructureBuildInput geometry{};
    geometry.type      = rhi::AccelerationStructureBuildInputType::Instances;
    geometry.instances = {
        instanceBuffer, sizeof(rhi::AccelerationStructureInstanceDescGeneric), uint32_t(instances.size())
    };
    rhi::AccelerationStructureBuildDesc build;
    build.inputs     = &geometry;
    build.inputCount = 1;
    build.flags      = rhi::AccelerationStructureBuildFlags::PreferFastTrace;

    rhi::AccelerationStructureSizes sizes;
    rhiCheck(device.getAccelerationStructureSizes(build, &sizes));
    rhi::AccelerationStructureDesc tlasDesc;
    tlasDesc.kind = rhi::AccelerationStructureKind::TopLevel;
    tlasDesc.size = sizes.accelerationStructureSize;
    rhi::ComPtr<rhi::IAccelerationStructure> result;
    rhiCheck(device.createAccelerationStructure(tlasDesc, result.writeRef()));

    rhi::ComPtr<rhi::IBuffer> scratch;
    rhi::BufferDesc scratchDesc;
    scratchDesc.size  = sizes.scratchSize;
    scratchDesc.usage = rhi::BufferUsage::UnorderedAccess;
    rhiCheck(device.createBuffer(scratchDesc, nullptr, scratch.writeRef()));
    commands.buildAccelerationStructure(build, result, nullptr, scratch, 0, nullptr);
    return result;
}
