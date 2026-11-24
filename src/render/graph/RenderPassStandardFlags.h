#pragma once

#include <cstdint>

inline constexpr char kRenderPassRefreshFlags[]  = "_refreshFlags";
inline constexpr char kRenderPassPrngDimension[] = "_prngDimension";

enum class RenderPassRefreshFlags : uint32_t
{
    None                 = 0,
    RenderOptionsChanged = 1 << 1,
};
