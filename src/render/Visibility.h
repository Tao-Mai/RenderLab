#pragma once

#include <bit>
#include <format>
#include "core/Logger.h"
#include "render/resource/ShaderData.h"

namespace RenderVisibility
{
inline constexpr uint32_t kMainView          = 1u;
inline constexpr uint32_t kPointOffset       = 1;
inline constexpr uint32_t kSpotOffset        = kPointOffset + kMaxPointLightShadow;
inline constexpr uint32_t kRectAreaOffset    = kSpotOffset + kMaxSpotLightShadow;
inline constexpr uint32_t kDirectionalOffset = kRectAreaOffset + kMaxRectAreaLightShadow;
inline constexpr uint32_t kBitCount = kDirectionalOffset + kMaxDirectionalLightShadow * kMaxDirectionalShadowCascades;
static_assert(kBitCount <= 32);

[[nodiscard]] inline std::string viewName(uint32_t mask)
{
    DCHECK(std::has_single_bit(mask));
    const uint32_t bit = std::countr_zero(mask);
    if (bit == 0)
        return "Main view";
    if (bit < kSpotOffset)
        return std::format("Point shadow {}", bit - kPointOffset);
    if (bit < kRectAreaOffset)
        return std::format("Spot shadow {}", bit - kSpotOffset);
    if (bit < kDirectionalOffset)
        return std::format("Area shadow {}", bit - kRectAreaOffset);
    return std::format(
        "Directional {} cascade {}",
        (bit - kDirectionalOffset) / kMaxDirectionalShadowCascades,
        (bit - kDirectionalOffset) % kMaxDirectionalShadowCascades
    );
}

[[nodiscard]] inline uint32_t shadowMask(GpuLightType type, uint32_t shadowIndex, uint32_t cascadeIndex = 0)
{
    switch (type)
    {
        case GpuLightType::Point:
            DCHECK(shadowIndex < kMaxPointLightShadow);
            return 1u << (kPointOffset + shadowIndex);
        case GpuLightType::Spot:
            DCHECK(shadowIndex < kMaxSpotLightShadow);
            return 1u << (kSpotOffset + shadowIndex);
        case GpuLightType::RectArea:
            DCHECK(shadowIndex < kMaxRectAreaLightShadow);
            return 1u << (kRectAreaOffset + shadowIndex);
        case GpuLightType::Directional:
            DCHECK(shadowIndex < kMaxDirectionalLightShadow && cascadeIndex < kMaxDirectionalShadowCascades);
            return 1u << (kDirectionalOffset + shadowIndex * kMaxDirectionalShadowCascades + cascadeIndex);
    }
    DCHECK(false);
    return 0;
}
}  // namespace RenderVisibility
