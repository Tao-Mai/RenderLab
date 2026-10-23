#include "render/raytracing/BlasBuildPass.h"

#include "core/Vertex.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/RenderResourceManager.h"

#include <array>

RenderGraph::AccelerationStructureHandle BlasBuildPass::setup(RenderGraph& graph, const PassSetup& setup)
{
    CHECK(setup.vulkan.supportsRayTracing());
    // Cached BLAS and scratch storage survive graph rebuilds.
    output = graph.write(graph.createAccelerationStructure({
        .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
            vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        .access =
            vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
    }));
    graph.use(
        output,
        {
            .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
        }
    );
    return output;
}

void BlasBuildPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame)
{
    const auto&                         vulkan = frame.setup.vulkan;
    const auto&                         device = vulkan.deviceHandle();
    std::vector<AccelerationStructure*> structures;
    structures.reserve(frame.renderData.drawItems.size() + 1);
    for (const auto& item : frame.renderData.drawItems)
    {
        auto&       mesh    = renderContext().resources->mesh(item.mesh);
        const auto& submesh = mesh.submeshes()[item.submeshIndex];
        CHECK(submesh.indexCount % 3 == 0, "ray tracing submesh requires triangle indices");
        const auto [entry, created] = meshes.try_emplace({&mesh, submesh.firstIndex, submesh.indexCount});
        structures.push_back(&entry->second);
        if (!created)
            continue;

        VkAccelerationStructureGeometryKHR geometry{
            .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
        };
        geometry.geometry.triangles = {
            .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData   = {.deviceAddress = mesh.vertices().address(device)},
            .vertexStride = sizeof(Vertex),
            .maxVertex    = mesh.vertexCount() - 1,
            .indexType    = VK_INDEX_TYPE_UINT32,
            .indexData    = {.deviceAddress = mesh.indices().address(device) + submesh.firstIndex * sizeof(uint32_t)},
        };
        auto& blas = entry->second;
        blas.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, submesh.indexCount / 3);
        blas.build(vulkan, commands, geometry, submesh.indexCount / 3);
    }

    const auto& ranges = frame.renderData.lightRanges;
    if (ranges.rectAreaEnd > ranges.pointEnd)
    {
        if (!rectLightBlas.handle())
            createRectLightGeometry(commands, vulkan);
        structures.push_back(&rectLightBlas);
    }
    graph.bindAccelerationStructures(output, structures);
}

void BlasBuildPass::createRectLightGeometry(vk::raii::CommandBuffer& commands, const VulkanContext& vulkan)
{
    const auto& device = vulkan.deviceHandle();
    // A unit XY rectangle facing local -Z. Every light shares this BLAS.
    constexpr std::array<float, 12>
        kVertices{-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f};
    constexpr std::array<uint32_t, 6> kIndices{0, 2, 1, 0, 3, 2};
    const auto                        usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
        vk::BufferUsageFlagBits::eShaderDeviceAddress;
    const auto host   = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    rectLightVertices = Buffer(vulkan.physicalDeviceHandle(), device, sizeof(kVertices), usage, host);
    rectLightIndices  = Buffer(vulkan.physicalDeviceHandle(), device, sizeof(kIndices), usage, host);
    rectLightVertices.upload(kVertices.data(), sizeof(kVertices));
    rectLightIndices.upload(kIndices.data(), sizeof(kIndices));

    VkAccelerationStructureGeometryKHR geometry{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
    };
    geometry.geometry.triangles = {
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData   = {.deviceAddress = rectLightVertices.address(device)},
        .vertexStride = sizeof(float) * 3,
        .maxVertex    = static_cast<uint32_t>(kVertices.size() / 3 - 1),
        .indexType    = VK_INDEX_TYPE_UINT32,
        .indexData    = {.deviceAddress = rectLightIndices.address(device)},
    };
    constexpr uint32_t kTriangleCount = kIndices.size() / 3;
    rectLightBlas.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, kTriangleCount);
    rectLightBlas.build(vulkan, commands, geometry, kTriangleCount);
}
