#pragma once

#include <slang-rhi.h>
#include "core/Logger.h"

inline void rhiCheck(SlangResult result)
{
    CHECK(SLANG_SUCCEEDED(result), "slang-rhi failed: {:#x}", static_cast<uint32_t>(result));
}
