#include "render/raytracing/TlasBuildPass.h"

#include "render/RenderData.h"
#include "render/device/VulkanContext.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <vector>

namespace
{
// Match the instance masks and hit-group order in both ray tracing shaders.
constexpr uint32_t kSceneMask          = 0x01;
constexpr uint32_t kLightMask          = 0x02;
constexpr uint32_t kLightHitRecord     = 1;
constexpr uint32_t kInstanceIndexLimit = 1u << 24;

VkTransformMatrixKHR instanceTransform(const glm::mat4& model)
{
    VkTransformMatrixKHR transform{};
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            transform.matrix[row][column] = model[column][row];
    return transform;
}
}  // namespace

TlasBuildPass::Output
TlasBuildPass::setup(RenderGraph& graph, const PassSetup&, RenderGraph::AccelerationStructureHandle blas)
{
    graph.use(
        blas,
        {
            .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        }
    );
    const auto tlas = graph.write(graph.createAccelerationStructure({
        .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
            vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        .access =
            vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
    }));
    graph.use(
        tlas,
        {
            .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
        }
    );
    return {blas, tlas};
}

void TlasBuildPass::execute(RenderGraph&, CommandRecorder& commands, const PassFrame& execution)
{
    const auto& vulkan = execution.setup.vulkan;
    const auto& device = vulkan.deviceHandle();
    const auto& items  = execution.renderData.drawItems;
    CHECK(items.size() < kInstanceIndexLimit, "ray tracing instance count exceeds the custom-index limit");

    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(
        items.size() + execution.renderData.lightRanges.rectAreaEnd - execution.renderData.lightRanges.pointEnd
    );
    for (const auto& item : items)
    {
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform                      = instanceTransform(item.model);
        instance.instanceCustomIndex            = static_cast<uint32_t>(instances.size());
        instance.mask                           = kSceneMask;
        instance.flags                          = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = bottomLevels.mesh(item).address();
        instances.push_back(instance);
    }

    const auto& lights = execution.renderData.lights;
    const auto& ranges = execution.renderData.lightRanges;
    CHECK(lights.size() < kInstanceIndexLimit, "ray tracing light index exceeds the custom-index limit");
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
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform                              = instanceTransform(model);
        instance.instanceCustomIndex                    = index;
        instance.mask                                   = kLightMask;
        instance.instanceShaderBindingTableRecordOffset = kLightHitRecord;
        // One-sided emission retains facing culling, unlike scene geometry.
        instance.accelerationStructureReference = bottomLevels.rectLight().address();
        instances.push_back(instance);
    }

    auto&      frame = frames[execution.frameIndex];
    const auto count = static_cast<uint32_t>(instances.size());
    if (frame.instanceCount != count)
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
    VkAccelerationStructureGeometryKHR geometry{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
    };
    geometry.geometry.instances = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .data  = {.deviceAddress = frame.instances.address(device)},
    };
    if (frame.instanceCount != count)
    {
        frame.tlas.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geometry, count);
        frame.instanceCount = count;
    }
    if (count)
        frame.instances.upload(instances.data(), instances.size() * sizeof(instances.front()));

    frame.tlas.build(vulkan, commands, geometry, count);
}
