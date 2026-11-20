#include "render/raytracing/SceneAccelerationStructures.h"

#include "core/Logger.h"
#include "core/Vertex.h"
#include "render/RenderData.h"
#include "render/device/VulkanContext.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace
{
// Match the instance masks and hit-group order in both ray tracing shaders.
constexpr uint32_t kSceneMask          = 0x01;
constexpr uint32_t kLightMask          = 0x02;
constexpr uint32_t kLightHitRecord     = 1;
constexpr uint32_t kInstanceIndexLimit = 1u << 24;

VkAccelerationStructureGeometryKHR triangleGeometry(
    vk::DeviceAddress vertices, vk::DeviceSize vertexStride, uint32_t vertexCount, vk::DeviceAddress indices
)
{
    return {
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry     = {.triangles = {
            .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData   = {.deviceAddress = vertices},
            .vertexStride = vertexStride,
            .maxVertex    = vertexCount - 1,
            .indexType    = VK_INDEX_TYPE_UINT32,
            .indexData    = {.deviceAddress = indices},
        }},
        .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
    };
}

VkTransformMatrixKHR instanceTransform(const glm::mat4& model)
{
    VkTransformMatrixKHR transform{};
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            transform.matrix[row][column] = model[column][row];
    return transform;
}

std::vector<VkAccelerationStructureInstanceKHR> makeInstances(
    const RenderData& renderData, std::span<const vk::DeviceAddress> meshBlasAddresses,
    vk::DeviceAddress rectLightBlasAddress
)
{
    const auto& lights = renderData.lights;
    const auto& ranges = renderData.lightRanges;
    const auto  sceneInstanceCount = renderData.drawItems.size();
    CHECK(sceneInstanceCount < kInstanceIndexLimit, "ray tracing instance count exceeds the custom-index limit");
    CHECK(lights.size() < kInstanceIndexLimit, "ray tracing light index exceeds the custom-index limit");

    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(sceneInstanceCount + ranges.rectAreaEnd - ranges.pointEnd);
    for (const auto& item : renderData.drawItems)
    {
        const auto index = instances.size();
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform                      = instanceTransform(item.model);
        instance.instanceCustomIndex            = static_cast<uint32_t>(index);
        instance.mask                           = kSceneMask;
        instance.flags                          = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = meshBlasAddresses[index];
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
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform                              = instanceTransform(model);
        instance.instanceCustomIndex                    = index;
        instance.mask                                   = kLightMask;
        instance.instanceShaderBindingTableRecordOffset = kLightHitRecord;
        instance.accelerationStructureReference          = rectLightBlasAddress;
        instances.push_back(instance);
    }
    return instances;
}

void makeBuildWritesVisible(vk::raii::CommandBuffer& commands, vk::PipelineStageFlagBits2 reader)
{
    const vk::MemoryBarrier2 barrier{
        .srcStageMask  = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
        .dstStageMask  = reader,
        .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
    };
    commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
}
}  // namespace

const AccelerationStructure& SceneAccelerationStructures::recordBuilds(
    const VulkanContext& vulkan, vk::raii::CommandBuffer& commands, RenderResourceManager& resources,
    const RenderData& renderData, uint32_t frameIndex
)
{
    DCHECK(frameIndex < frames.size());

    const auto meshBlasAddresses = recordBlasBuilds(vulkan, commands, resources, renderData);
    const auto instances = makeInstances(renderData, meshBlasAddresses, rectLightBlas.address());

    auto& frame = frames[frameIndex];
    recordTlasBuild(vulkan, commands, frame, instances);
    makeBuildWritesVisible(commands, vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
    return frame.tlas;
}

std::vector<vk::DeviceAddress> SceneAccelerationStructures::recordBlasBuilds(
    const VulkanContext& vulkan, vk::raii::CommandBuffer& commands, RenderResourceManager& resources,
    const RenderData& renderData
)
{
    const auto& device = vulkan.deviceHandle();
    std::vector<vk::DeviceAddress> addresses;
    addresses.reserve(renderData.drawItems.size());

    bool builtBlas = false;
    for (const auto& item : renderData.drawItems)
    {
        const auto& mesh    = resources.mesh(item.mesh);
        const auto& submesh = mesh.submeshes()[item.submeshIndex];
        CHECK(
            submesh.indexCount % 3 == 0,
            "mesh '{}' submesh {} has {} indices for ray tracing",
            item.mesh.value, item.submeshIndex, submesh.indexCount
        );

        const auto [entry, created] = meshBlasCache.try_emplace({&mesh, submesh.firstIndex, submesh.indexCount});
        auto& blas = entry->second;
        if (created)
        {
            const auto geometry = triangleGeometry(
                mesh.vertices().address(device), sizeof(Vertex), mesh.vertexCount(),
                mesh.indices().address(device) + submesh.firstIndex * sizeof(uint32_t)
            );
            const auto triangleCount = submesh.indexCount / 3;
            blas.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, triangleCount);
            blas.build(vulkan, commands, geometry, triangleCount);
            builtBlas = true;
        }
        addresses.push_back(blas.address());
    }

    const auto& ranges = renderData.lightRanges;
    if (ranges.rectAreaEnd > ranges.pointEnd && !rectLightBlas.handle())
    {
        recordRectLightBlasBuild(vulkan, commands);
        builtBlas = true;
    }
    if (builtBlas)
        makeBuildWritesVisible(commands, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);

    return addresses;
}

void SceneAccelerationStructures::recordRectLightBlasBuild(
    const VulkanContext& vulkan, vk::raii::CommandBuffer& commands
)
{
    // A unit XY rectangle facing local -Z. Every light shares this BLAS.
    constexpr std::array<float, 12>
        kVertices{-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f};
    constexpr std::array<uint32_t, 6> kIndices{0, 2, 1, 0, 3, 2};
    constexpr uint32_t kVertexCount = kVertices.size() / 3;
    constexpr uint32_t kTriangleCount = kIndices.size() / 3;
    constexpr auto kUsage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
        vk::BufferUsageFlagBits::eShaderDeviceAddress;
    constexpr auto kMemory = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

    const auto& physicalDevice = vulkan.physicalDeviceHandle();
    const auto& device         = vulkan.deviceHandle();
    rectLightVertices = Buffer(physicalDevice, device, sizeof(kVertices), kUsage, kMemory);
    rectLightIndices  = Buffer(physicalDevice, device, sizeof(kIndices), kUsage, kMemory);
    rectLightVertices.upload(kVertices.data(), sizeof(kVertices));
    rectLightIndices.upload(kIndices.data(), sizeof(kIndices));

    const auto geometry = triangleGeometry(
        rectLightVertices.address(device), sizeof(float) * 3, kVertexCount, rectLightIndices.address(device)
    );
    rectLightBlas.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, kTriangleCount);
    rectLightBlas.build(vulkan, commands, geometry, kTriangleCount);
}

void SceneAccelerationStructures::recordTlasBuild(
    const VulkanContext& vulkan, vk::raii::CommandBuffer& commands, Frame& frame,
    std::span<const VkAccelerationStructureInstanceKHR> instances
)
{
    const auto& device = vulkan.deviceHandle();
    const auto  count = static_cast<uint32_t>(instances.size());
    const bool  recreate = !frame.tlas.handle() || frame.instanceCount != count;
    if (recreate)
    {
        frame.tlas.reset();
        frame.instances.reset();
        frame.instances = Buffer(
            vulkan.physicalDeviceHandle(),
            device,
            std::max(count, 1u) * sizeof(VkAccelerationStructureInstanceKHR),
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
    }

    const VkAccelerationStructureGeometryKHR geometry{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry     = {.instances = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .data  = {.deviceAddress = frame.instances.address(device)},
        }},
    };
    if (recreate)
    {
        frame.tlas.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geometry, count);
        frame.instanceCount = count;
    }
    if (!instances.empty())
        frame.instances.upload(instances.data(), instances.size_bytes());

    frame.tlas.build(vulkan, commands, geometry, count);
}

void SceneAccelerationStructures::reset() noexcept
{
    for (auto& frame : frames)
    {
        frame.tlas.reset();
        frame.instances.reset();
        frame.instanceCount = 0;
    }
    rectLightBlas.reset();
    rectLightIndices.reset();
    rectLightVertices.reset();
    meshBlasCache.clear();
}
