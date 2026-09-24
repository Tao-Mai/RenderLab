#include "render/resource/Buffer.h"

#include "render/device/Memory.h"
#include "render/device/VkCheck.h"

#include <cstring>
#include "core/Logger.h"

Buffer::Buffer(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device&         device,
    vk::DeviceSize                  size,
    vk::BufferUsageFlags            usage,
    vk::MemoryPropertyFlags         memoryProperties) :
    byteSize(size)
{
    CHECK(size != 0, "buffer size must be greater than zero");

    const vk::BufferCreateInfo bufferInfo{
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };
    buffer = vkCheck(device.createBuffer(bufferInfo));

    const vk::MemoryRequirements requirements = buffer.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(
            physicalDevice,
            requirements.memoryTypeBits,
            memoryProperties)
    };

    memory = vkCheck(device.allocateMemory(allocationInfo));
    vkCheck(buffer.bindMemory(*memory, 0));
}

void Buffer::upload(const void* data, vk::DeviceSize byteCount)
{
    CHECK(data != nullptr && byteCount != 0 && byteCount <= byteSize,
        "invalid buffer upload");

    void* mappedMemory = vkCheck(memory.mapMemory(0, byteCount));
    std::memcpy(mappedMemory, data, static_cast<std::size_t>(byteCount));
    memory.unmapMemory();
}

void Buffer::download(void* data, vk::DeviceSize byteCount)
{
    CHECK(data != nullptr && byteCount != 0 && byteCount <= byteSize,
        "invalid buffer download");

    void* mappedMemory = vkCheck(memory.mapMemory(0, byteCount));
    std::memcpy(data, mappedMemory, static_cast<std::size_t>(byteCount));
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
