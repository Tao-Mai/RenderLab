#pragma once

#include "render/device/RhiCheck.h"

#include <algorithm>
#include <cstdint>
#include <utility>

inline void ensureBufferCapacity(rhi::IDevice& device, rhi::ComPtr<rhi::IBuffer>& buffer, uint64_t minimumSize)
{
    auto desc = buffer->getDesc();
    if (desc.size >= minimumSize)
        return;

    desc.size = std::max(minimumSize, desc.size * 2);
    rhi::ComPtr<rhi::IBuffer> replacement;
    rhiCheck(device.createBuffer(desc, nullptr, replacement.writeRef()));
    buffer = std::move(replacement);
}
