#pragma once

#include <vulkan/vulkan_raii.hpp>

class Buffer
{
  public:
    Buffer() = default;
    Buffer(
        const vk::raii::PhysicalDevice &physicalDevice,
        const vk::raii::Device &device,
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags memoryProperties);

    Buffer(const Buffer &)            = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&) noexcept        = default;
    Buffer &operator=(Buffer &&) noexcept = default;

    void upload(const void *data, vk::DeviceSize byteCount);
    void download(void *data, vk::DeviceSize byteCount);
    void reset() noexcept;

    [[nodiscard]] vk::Buffer handle() const;
    [[nodiscard]] vk::DeviceSize size() const;

  private:
    vk::DeviceSize         byteSize = 0;
    vk::raii::DeviceMemory memory   = nullptr;
    vk::raii::Buffer       buffer   = nullptr;

    static uint32_t findMemoryType(
        const vk::raii::PhysicalDevice &physicalDevice,
        uint32_t typeFilter,
        vk::MemoryPropertyFlags requiredProperties);
};
