#pragma once

#include "core/Object.h"

namespace vk::raii
{
class CommandBuffer;
}

class VulkanResources;

namespace Falcor
{
class Device;

class FALCOR_API CopyContext
{
public:
    explicit CopyContext(Device* device);
    virtual ~CopyContext();

    [[nodiscard]] ref<Device> getDevice() const;

    VulkanResources*         vulkanResources = nullptr;
    vk::raii::CommandBuffer* commands        = nullptr;

protected:
    Device* mpDevice;
};
}  // namespace Falcor
