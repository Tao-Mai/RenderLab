#pragma once

#include "render/resources/Buffer.h"

#include <cstdint>
#include <span>
#include <vector>

class VulkanContext;

class RayTracingPipeline
{
public:
    struct Description
    {
        vk::ShaderModule             shader;
        vk::PipelineLayout           layout;
        std::span<const char* const> raygen;
        std::span<const char* const> miss;
        std::span<const char* const> closestHit;
        uint32_t                    maxRecursionDepth;
    };

    RayTracingPipeline(const VulkanContext& vulkan, const Description& description);

    [[nodiscard]] vk::PipelineLayout layout() const { return pipelineLayout; }
    void bind(vk::raii::CommandBuffer& commands) const;
    void dispatch(vk::raii::CommandBuffer& commands, uint32_t raygenIndex, vk::Extent3D extent) const;

private:
    vk::PipelineLayout pipelineLayout;
    vk::raii::Pipeline pipeline = nullptr;
    Buffer bindingTable;
    std::vector<vk::StridedDeviceAddressRegionKHR> raygenRegions;
    vk::StridedDeviceAddressRegionKHR missRegion;
    vk::StridedDeviceAddressRegionKHR hitRegion;

    void buildBindingTable(
        const VulkanContext& vulkan,
        const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& limits,
        uint32_t raygenCount, uint32_t missCount, uint32_t hitCount
    );
};
