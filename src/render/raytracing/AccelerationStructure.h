#pragma once

#include "render/resource/Buffer.h"

class VulkanContext;

class AccelerationStructure
{
public:
    void create(const VulkanContext& vulkan, VkAccelerationStructureTypeKHR type,
        const VkAccelerationStructureGeometryKHR& geometry, uint32_t primitiveCount);
    void build(const VulkanContext& vulkan, vk::raii::CommandBuffer& commands,
        const VkAccelerationStructureGeometryKHR& geometry, uint32_t primitiveCount) const;
    void reset() noexcept;

    [[nodiscard]] vk::AccelerationStructureKHR handle() const { return *structure; }
    [[nodiscard]] vk::DeviceAddress address() const { return deviceAddress; }

private:
    Buffer storage;
    Buffer scratch;
    vk::raii::AccelerationStructureKHR structure = nullptr;
    VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    vk::DeviceAddress deviceAddress = 0;
    vk::DeviceAddress scratchAddress = 0;
};
