#pragma once


#include <vulkan/vulkan_raii.hpp>
#include "core/Logger.h"

namespace vulkan_memory
{
inline uint32_t findType(
    const vk::raii::PhysicalDevice& physicalDevice,
    uint32_t                        typeFilter,
    vk::MemoryPropertyFlags         requiredProperties)
{
    const vk::PhysicalDeviceMemoryProperties memoryProperties =
        physicalDevice.getMemoryProperties();

    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
    {
        const bool supported = (typeFilter & (1u << index)) != 0;
        const bool hasRequiredProperties =
            (memoryProperties.memoryTypes[index].propertyFlags & requiredProperties) ==
            requiredProperties;
        if (supported && hasRequiredProperties)
        {
            return index;
        }
    }

    LOG_FATAL("failed to find a suitable Vulkan memory type");
}
}
