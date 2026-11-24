#ifndef RENDERLAB_PATH_TRACING_PARAMETERS_H
#define RENDERLAB_PATH_TRACING_PARAMETERS_H

#ifdef __cplusplus
#include "core/MetadataAnnotations.h"

#include <cstdint>
#include <glm/vec4.hpp>

inline constexpr uint32_t kMinPathBounces = 1;
inline constexpr uint32_t kMaxPathBounces = 64;

#define RENDERLAB_PATH_ALIGN alignas(16) RL_SHADER_LAYOUT
#define RENDERLAB_PATH_FLOAT4 glm::vec4
#define RENDERLAB_PATH_UINT uint32_t
#else
#define RENDERLAB_PATH_ALIGN
#define RENDERLAB_PATH_FLOAT4 float4
#define RENDERLAB_PATH_UINT uint
#endif

struct RENDERLAB_PATH_ALIGN PathTracingParameters
{
    RENDERLAB_PATH_FLOAT4 viewport;
    RENDERLAB_PATH_UINT maxBounces;
    RENDERLAB_PATH_UINT sampleIndex;
    RENDERLAB_PATH_UINT prngDimension;
    RENDERLAB_PATH_UINT padding1;

#ifndef __cplusplus
    property float2 viewportOffset
    {
        get { return viewport.xy; }
    }
    property float2 viewportExtent
    {
        get { return viewport.zw; }
    }

    bool containsPixel(uint2 pixel)
    {
        float2 local = float2(pixel) + 0.5 - viewportOffset;
        return all(local >= 0.0) && all(local < viewportExtent);
    }
#endif
};

#undef RENDERLAB_PATH_ALIGN
#undef RENDERLAB_PATH_FLOAT4
#undef RENDERLAB_PATH_UINT

#endif
