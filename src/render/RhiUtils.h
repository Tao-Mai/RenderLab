#pragma once

#include <algorithm>
#include <cstdint>
#include <slang-rhi.h>
#include <utility>

#include "core/Logger.h"

inline void rhiCheck(SlangResult result)
{
    CHECK(SLANG_SUCCEEDED(result), "slang-rhi failed: {:#x}", static_cast<uint32_t>(result));
}

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
