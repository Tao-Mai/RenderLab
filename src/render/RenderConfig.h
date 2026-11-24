#pragma once

#include <cstdint>
#include <slang-rhi.h>

inline constexpr uint32_t    kMaxFramesInFlight            = 2;
inline constexpr uint32_t    kMaxLights                    = 65536;
inline constexpr uint32_t    kMaxDirectionalLightShadow    = 1;
inline constexpr uint32_t    kMaxDirectionalShadowCascades = 4;
inline constexpr uint32_t    kMaxPointLightShadow          = 4;
inline constexpr uint32_t    kMaxSpotLightShadow           = 4;
inline constexpr uint32_t    kMaxRectAreaLightShadow       = 4;
inline constexpr uint32_t    kRayPipelineRecursionDepth    = 1;
inline constexpr uint32_t    kMinPathBounces               = 1;
inline constexpr uint32_t    kMaxPathBounces               = 64;
inline constexpr rhi::Format kHdrColorFormat               = rhi::Format::RGBA16Float;

inline constexpr uint32_t kPathTracingPayloadBytes = 256;
