#pragma once

#include "render/resources/NativeHandle.h"

#include <bit>
#include <vulkan/vulkan.h>

namespace Falcor
{
#define FALCOR_NATIVE_HANDLE(Handle, Type)                                           \
    template <>                                                                    \
    struct NativeHandleTrait<Handle>                                                \
    {                                                                              \
        static constexpr NativeHandleType type = Type;                              \
                                                                                   \
        static uint64_t pack(Handle handle) { return std::bit_cast<uint64_t>(handle); } \
        static Handle unpack(uint64_t value) { return std::bit_cast<Handle>(value); }   \
    }

FALCOR_NATIVE_HANDLE(VkImage, NativeHandleType::VkImage);
FALCOR_NATIVE_HANDLE(VkImageView, NativeHandleType::VkImageView);
FALCOR_NATIVE_HANDLE(VkBuffer, NativeHandleType::VkBuffer);

#undef FALCOR_NATIVE_HANDLE
}  // namespace Falcor
