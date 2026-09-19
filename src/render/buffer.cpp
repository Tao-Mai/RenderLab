#include "render/buffer.h"

#include <cstring>
#include <stdexcept>

Buffer::Buffer(
    const vk::raii::PhysicalDevice &physicalDevice,
    const vk::raii::Device &device,
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags memoryProperties) :
    byteSize(size)
{
    if (size == 0)
    {
        throw std::invalid_argument("buffer size must be greater than zero");
    }

    const vk::BufferCreateInfo bufferInfo{
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };
    buffer = vk::raii::Buffer(device, bufferInfo);

    const vk::MemoryRequirements requirements = buffer.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(
            physicalDevice,
            requirements.memoryTypeBits,
            memoryProperties)
    };

    memory = vk::raii::DeviceMemory(device, allocationInfo);
    buffer.bindMemory(*memory, 0);
}

void Buffer::upload(const void *data, vk::DeviceSize byteCount)
{
    if (data == nullptr || byteCount == 0 || byteCount > byteSize)
    {
        throw std::invalid_argument("invalid buffer upload");
    }

    void *mappedMemory = memory.mapMemory(0, byteCount);
    std::memcpy(mappedMemory, data, static_cast<std::size_t>(byteCount));
    memory.unmapMemory();
}

void Buffer::reset() noexcept
{
    buffer   = nullptr;
    memory   = nullptr;
    byteSize = 0;
}

vk::Buffer Buffer::handle() const
{
    return *buffer;
}

vk::DeviceSize Buffer::size() const
{
    return byteSize;
}

uint32_t Buffer::findMemoryType(
    const vk::raii::PhysicalDevice &physicalDevice,
    uint32_t typeFilter,
    vk::MemoryPropertyFlags requiredProperties)
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

    throw std::runtime_error("failed to find a suitable Vulkan memory type");
}
