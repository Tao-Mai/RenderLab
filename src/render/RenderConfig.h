#pragma once

#include <cstdint>
#include <slang-rhi.h>

inline constexpr uint32_t kMaxFramesInFlight = 2;
inline constexpr rhi::Format kHdrColorFormat = rhi::Format::RGBA16Float;
