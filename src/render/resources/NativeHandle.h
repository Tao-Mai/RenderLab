#pragma once

#include "core/Logger.h"

#include <cstdint>

namespace Falcor
{
enum class NativeHandleType
{
    Unknown,
    ID3D12Device,
    ID3D12Resource,
    ID3D12PipelineState,
    ID3D12Fence,
    ID3D12CommandQueue,
    ID3D12GraphicsCommandList,
    D3D12_CPU_DESCRIPTOR_HANDLE,
    VkInstance,
    VkPhysicalDevice,
    VkDevice,
    VkImage,
    VkImageView,
    VkBuffer,
    VkBufferView,
    VkPipeline,
    VkFence,
    VkQueue,
    VkCommandBuffer,
    VkSampler,
};

template <typename T>
struct NativeHandleTrait;

class NativeHandle
{
public:
    NativeHandle() = default;

    template <typename T>
    explicit NativeHandle(T native) : mType(NativeHandleTrait<T>::type), mValue(NativeHandleTrait<T>::pack(native))
    {
    }

    [[nodiscard]] NativeHandleType getType() const { return mType; }
    [[nodiscard]] bool isValid() const { return mType != NativeHandleType::Unknown; }

    template <typename T>
    [[nodiscard]] T as() const
    {
        DCHECK(mType == NativeHandleTrait<T>::type);
        return NativeHandleTrait<T>::unpack(mValue);
    }

private:
    NativeHandleType mType  = NativeHandleType::Unknown;
    uint64_t         mValue = 0;
};
}  // namespace Falcor
