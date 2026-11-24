#include "render/raytracing/SceneAccelerationStructures.h"

#include <slang-rhi.h>
#include "core/Logger.h"
#include "core/Vertex.h"
#include "render/PreparedScene.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/geometric.hpp>
#include "render/device/RhiCheck.h"

namespace
{
// Match the instance masks and hit-group order in both ray tracing shaders.
constexpr uint32_t kSceneMask          = 0x01;
constexpr uint32_t kLightMask          = 0x02;
constexpr uint32_t kLightHitRecord     = 1;
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
    const PreparedScene&                              renderData,
    std::span<const rhi::AccelerationStructureHandle> meshBlasAddresses,
    rhi::AccelerationStructureHandle                  rectLightBlasAddress
)
{
    const auto& lights             = renderData.lights;
    const auto& ranges             = renderData.lightRanges;
    const auto  sceneInstanceCount = renderData.drawItems.size();
    CHECK(sceneInstanceCount < kInstanceIndexLimit, "ray tracing instance count exceeds the custom-index limit");
    CHECK(lights.size() < kInstanceIndexLimit, "ray tracing light index exceeds the custom-index limit");

    std::vector<rhi::AccelerationStructureInstanceDescGeneric> instances;
    instances.reserve(sceneInstanceCount + ranges.rectAreaEnd - ranges.pointEnd);
    for (const auto& item : renderData.drawItems)
    {
        const auto                                    index = instances.size();
        rhi::AccelerationStructureInstanceDescGeneric instance{};
        setInstanceTransform(instance, item.model);
        instance.instanceID            = static_cast<uint32_t>(index);
        instance.instanceMask          = kSceneMask;
        instance.flags                 = rhi::AccelerationStructureInstanceFlags::TriangleFacingCullDisable;
        instance.accelerationStructure = meshBlasAddresses[index];
        instances.push_back(instance);
    }

    for (uint32_t index = ranges.pointEnd; index < ranges.rectAreaEnd; ++index)
    {
        const auto&     light = lights[index];
        const glm::vec2 size{light.areaSizeCone};
        CHECK(
            std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0,
            "rectangular light {} requires finite positive dimensions",
            index
        );

        const glm::vec3 right = light.areaRight();
        const glm::vec3 direction{light.direction};
        const glm::vec3 up = glm::cross(right, direction);
        const glm::mat4 model{
            glm::vec4{right * size.x, 0},
            glm::vec4{up * size.y, 0},
            glm::vec4{-direction, 0},
            glm::vec4{light.position(), 1},
        };
        // One-sided emission retains facing culling, unlike scene geometry.
        rhi::AccelerationStructureInstanceDescGeneric instance{};
        setInstanceTransform(instance, model);
        instance.instanceID                          = index;
        instance.instanceMask                        = kLightMask;
        instance.instanceContributionToHitGroupIndex = kLightHitRecord;
        instance.accelerationStructure               = rectLightBlasAddress;
        instances.push_back(instance);
    }
    return instances;
}

}  // namespace

rhi::IAccelerationStructure* SceneAccelerationStructures::recordBuilds(
    rhi::IDevice&          device,
    rhi::ICommandEncoder&  commands,
    RenderResourceManager& resources,
    const PreparedScene&   renderData,
    uint32_t               frameIndex
)
{

    const auto meshBlasAddresses = recordBlasBuilds(device, commands, resources, renderData);
    const auto instances         = makeInstances(
        renderData,
        meshBlasAddresses,
        rectLightBlas ? rectLightBlas->getHandle() : rhi::AccelerationStructureHandle{}
    );

    auto& frame = frames[frameIndex];
    recordTlasBuild(device, commands, frame, instances);
    commands.globalBarrier();
    return frame.tlas;
}

std::vector<rhi::AccelerationStructureHandle> SceneAccelerationStructures::recordBlasBuilds(
    rhi::IDevice&          device,
    rhi::ICommandEncoder&  commands,
    RenderResourceManager& resources,
    const PreparedScene&   renderData
)
{
    std::vector<rhi::AccelerationStructureHandle> addresses;
    addresses.reserve(renderData.drawItems.size());

    bool builtBlas = false;
    for (const auto& item : renderData.drawItems)
    {
        const auto& mesh    = resources.mesh(item.mesh);
        const auto& submesh = mesh.submeshes()[item.submeshIndex];
        CHECK(
            submesh.indexCount % 3 == 0,
            "mesh '{}' submesh {} has {} indices for ray tracing",
            item.mesh.value,
            item.submeshIndex,
            submesh.indexCount
        );

        const auto [entry, created] = meshBlasCache.try_emplace({&mesh, submesh.firstIndex, submesh.indexCount});
        auto& blas                  = entry->second;
        if (created)
        {
            const auto geometry = triangleGeometry(
                mesh.vertices(),
                sizeof(Vertex),
                mesh.vertexCount(),
                mesh.indices(),
                submesh.firstIndex * sizeof(uint32_t),
                submesh.indexCount
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

    const auto& ranges = renderData.lightRanges;
    if (ranges.rectAreaEnd > ranges.pointEnd && !rectLightBlas)
    {
        recordRectLightBlasBuild(device, commands);
        builtBlas = true;
    }
    if (builtBlas)
        commands.globalBarrier();

    return addresses;
}

void SceneAccelerationStructures::recordRectLightBlasBuild(rhi::IDevice& device, rhi::ICommandEncoder& commands)
{
    constexpr std::array<float, 12>
        kVertices{-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f};
    constexpr std::array<uint32_t, 6> kIndices{0, 2, 1, 0, 3, 2};
    rhi::ComPtr<rhi::IBuffer> vertices;
    rhi::ComPtr<rhi::IBuffer> indices;
    rhi::BufferDesc          desc;
    desc.size  = sizeof(kVertices);
    desc.usage = rhi::BufferUsage::AccelerationStructureBuildInput;
    rhiCheck(device.createBuffer(desc, kVertices.data(), vertices.writeRef()));
    desc.size = sizeof(kIndices);
    rhiCheck(device.createBuffer(desc, kIndices.data(), indices.writeRef()));
    const auto geometry =
        triangleGeometry(vertices, sizeof(float) * 3, 4, indices, 0, uint32_t(kIndices.size()));

    rhi::AccelerationStructureBuildDesc build;
    build.inputs     = &geometry;
    build.inputCount = 1;
    build.flags      = rhi::AccelerationStructureBuildFlags::PreferFastTrace;
    rhi::AccelerationStructureSizes sizes;
    rhiCheck(device.getAccelerationStructureSizes(build, &sizes));
    rhi::AccelerationStructureDesc asDesc;
    asDesc.kind = rhi::AccelerationStructureKind::BottomLevel;
    asDesc.size = sizes.accelerationStructureSize;
    rhiCheck(device.createAccelerationStructure(asDesc, rectLightBlas.writeRef()));

    rhi::ComPtr<rhi::IBuffer> scratch;
    rhi::BufferDesc scratchDesc;
    scratchDesc.size  = sizes.scratchSize;
    scratchDesc.usage = rhi::BufferUsage::UnorderedAccess;
    rhiCheck(device.createBuffer(scratchDesc, nullptr, scratch.writeRef()));
    commands.buildAccelerationStructure(build, rectLightBlas, nullptr, scratch, 0, nullptr);
}

void SceneAccelerationStructures::recordTlasBuild(
    rhi::IDevice&                                                  device,
    rhi::ICommandEncoder&                                          commands,
    Frame&                                                         frame,
    std::span<const rhi::AccelerationStructureInstanceDescGeneric> instances
)
{
    // RHI requires at least one instance. A zero reference is an inactive instance for an empty scene.
    const auto count    = uint32_t(std::max<size_t>(instances.size(), 1));
    const bool recreate = !frame.tlas || frame.instanceCount != count;
    if (recreate)
    {
        rhi::BufferDesc desc;
        desc.size       = count * sizeof(rhi::AccelerationStructureInstanceDescGeneric);
        desc.memoryType = rhi::MemoryType::Upload;
        desc.usage      = rhi::BufferUsage::AccelerationStructureBuildInput;
        rhiCheck(device.createBuffer(desc, nullptr, frame.instances.writeRef()));
    }
    void* data;
    rhiCheck(device.mapBuffer(frame.instances, rhi::CpuAccessMode::Write, &data));
    if (instances.empty())
        std::memset(data, 0, sizeof(rhi::AccelerationStructureInstanceDescGeneric));
    else
        std::memcpy(data, instances.data(), instances.size_bytes());
    rhiCheck(device.unmapBuffer(frame.instances));
    rhi::AccelerationStructureBuildInput geometry{};
    geometry.type      = rhi::AccelerationStructureBuildInputType::Instances;
    geometry.instances = {frame.instances, sizeof(rhi::AccelerationStructureInstanceDescGeneric), count};
    rhi::AccelerationStructureBuildDesc build;
    build.inputs     = &geometry;
    build.inputCount = 1;
    build.flags      = rhi::AccelerationStructureBuildFlags::PreferFastTrace;
    if (recreate)
    {
        rhi::AccelerationStructureSizes sizes;
        rhiCheck(device.getAccelerationStructureSizes(build, &sizes));
        rhi::AccelerationStructureDesc desc;
        desc.kind = rhi::AccelerationStructureKind::TopLevel;
        desc.size = sizes.accelerationStructureSize;
        rhiCheck(device.createAccelerationStructure(desc, frame.tlas.writeRef()));
        rhi::BufferDesc scratch;
        scratch.size  = sizes.scratchSize;
        scratch.usage = rhi::BufferUsage::UnorderedAccess;
        rhiCheck(device.createBuffer(scratch, nullptr, frame.scratch.writeRef()));
        frame.instanceCount = count;
    }
    commands.buildAccelerationStructure(build, frame.tlas, nullptr, frame.scratch, 0, nullptr);
}

void SceneAccelerationStructures::reset() noexcept
{
    frames        = {};
    rectLightBlas.setNull();
    meshBlasCache.clear();
}
