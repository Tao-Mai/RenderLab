#pragma once

#include "render/RenderConfig.h"
#include "render/raytracing/AccelerationStructure.h"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <tuple>
#include <vector>

class GpuMesh;
class RenderResourceManager;
class VulkanContext;
struct RenderData;

class SceneAccelerationStructures
{
public:
    // Mesh uploads and the frame slot's previous submission must be complete.
    [[nodiscard]] const AccelerationStructure& recordBuilds(
        const VulkanContext& vulkan, vk::raii::CommandBuffer& commands, RenderResourceManager& resources,
        const RenderData& renderData, uint32_t frameIndex
    );
    void reset() noexcept;

private:
    struct Frame
    {
        AccelerationStructure tlas;
        Buffer                instances;
        uint32_t              instanceCount = 0;
    };

    std::map<std::tuple<const GpuMesh*, uint32_t, uint32_t>, AccelerationStructure> meshBlasCache;
    Buffer                                                                        rectLightVertices;
    Buffer                                                                        rectLightIndices;
    AccelerationStructure                                                         rectLightBlas;
    std::array<Frame, maxFramesInFlight>                                          frames;

    [[nodiscard]] std::vector<vk::DeviceAddress> recordBlasBuilds(
        const VulkanContext& vulkan, vk::raii::CommandBuffer& commands, RenderResourceManager& resources,
        const RenderData& renderData
    );
    void recordRectLightBlasBuild(const VulkanContext& vulkan, vk::raii::CommandBuffer& commands);
    void recordTlasBuild(
        const VulkanContext& vulkan, vk::raii::CommandBuffer& commands, Frame& frame,
        std::span<const VkAccelerationStructureInstanceKHR> instances
    );
};
