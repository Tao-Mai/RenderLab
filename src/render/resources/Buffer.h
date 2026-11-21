#pragma once

#include "render/resources/Resource.h"

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;

namespace Falcor
{
enum class MemoryType
{
    DeviceLocal,
    Upload,
    ReadBack,
};

class FALCOR_API Buffer : public Resource
{
    FALCOR_OBJECT(Buffer)

public:
    static constexpr uint64_t kEntireBuffer = ResourceViewInfo::kEntireBuffer;

    Buffer() : Resource({}, Type::Buffer, ResourceBindFlags::None, 0) {}
    Buffer(ref<Device> device, size_t size, ResourceBindFlags bindFlags, MemoryType memoryType, const void* initData);
    Buffer(
        const VulkanContext& vulkan,
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        MemoryType memoryType,
        ResourceBindFlags bindFlags
    );
    ~Buffer() override;

    Buffer(const Buffer&)                = delete;
    Buffer& operator=(const Buffer&)     = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    virtual void setBlob(const void* data, size_t offset, size_t size);
    void getBlob(void* data, size_t offset, size_t size) const;
    [[nodiscard]] void* map() const;
    void unmap() const;
    void  reset() noexcept;

    [[nodiscard]] ref<UnorderedAccessView> getUAV() override;
    [[nodiscard]] ref<UnorderedAccessView> getUAV(uint64_t offset, uint64_t size = kEntireBuffer);

    [[nodiscard]] vk::Buffer        handle() const { return *buffer; }
    [[nodiscard]] vk::BufferUsageFlags usage() const { return bufferUsage; }
    [[nodiscard]] MemoryType getMemoryType() const { return mMemoryType; }
    [[nodiscard]] uint64_t getGpuAddress() const;
    [[nodiscard]] size_t getSize() const { return mSize; }

private:
    vk::BufferUsageFlags  bufferUsage;
    MemoryType mMemoryType = MemoryType::DeviceLocal;
    mutable void* mMappedPtr = nullptr;
    vk::raii::DeviceMemory memory   = nullptr;
    vk::raii::Buffer       buffer   = nullptr;
};
}  // namespace Falcor
