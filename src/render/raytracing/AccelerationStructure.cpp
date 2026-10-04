#include "render/raytracing/AccelerationStructure.h"

#include "render/device/VulkanContext.h"
#include "render/device/VkCheck.h"

void AccelerationStructure::create(const VulkanContext& vulkan, VkAccelerationStructureTypeKHR targetType,
    const VkAccelerationStructureGeometryKHR& geometry, uint32_t primitiveCount)
{
    reset();
    type = targetType;
    const auto& device = vulkan.deviceHandle();
    const auto* api = device.getDispatcher();
    const VkDevice rawDevice = static_cast<VkDevice>(*device);
    const auto properties = vulkan.physicalDeviceHandle().getProperties2<
        vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
    const auto& limits = properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
    CHECK(primitiveCount <= (type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
        ? limits.maxInstanceCount : limits.maxPrimitiveCount),
        "acceleration structure input exceeds the device limit");
    const VkAccelerationStructureBuildGeometryInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = type,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry,
    };
    VkAccelerationStructureBuildSizesInfoKHR sizes{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    api->vkGetAccelerationStructureBuildSizesKHR(rawDevice,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &primitiveCount, &sizes);

    storage = Buffer(vulkan.physicalDeviceHandle(), device, sizes.accelerationStructureSize,
        vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    structure = vkCheck(device.createAccelerationStructureKHR({
        .buffer = storage.handle(), .size = storage.size(),
        .type = static_cast<vk::AccelerationStructureTypeKHR>(type),
    }));
    deviceAddress = device.getAccelerationStructureAddressKHR({.accelerationStructure = *structure});

    const vk::DeviceSize alignment = limits.minAccelerationStructureScratchOffsetAlignment;
    scratch = Buffer(vulkan.physicalDeviceHandle(), device, sizes.buildScratchSize + alignment,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    scratchAddress = (scratch.address(device) + alignment - 1) & ~(alignment - 1);
}

void AccelerationStructure::build(const VulkanContext& vulkan, vk::raii::CommandBuffer& commands,
    const VkAccelerationStructureGeometryKHR& geometry, uint32_t primitiveCount) const
{
    const VkAccelerationStructureBuildGeometryInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = type,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .dstAccelerationStructure = static_cast<VkAccelerationStructureKHR>(*structure),
        .geometryCount = 1,
        .pGeometries = &geometry,
        .scratchData = {.deviceAddress = scratchAddress},
    };
    const VkAccelerationStructureBuildRangeInfoKHR range{.primitiveCount = primitiveCount};
    const auto* ranges = &range;
    vulkan.deviceHandle().getDispatcher()->vkCmdBuildAccelerationStructuresKHR(
        static_cast<VkCommandBuffer>(*commands), 1, &info, &ranges);
}

void AccelerationStructure::reset() noexcept
{
    structure = nullptr;
    scratch.reset();
    storage.reset();
    deviceAddress = scratchAddress = 0;
}
