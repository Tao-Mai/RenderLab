#ifndef RENDERLAB_BDPT_BINDINGS_H
#define RENDERLAB_BDPT_BINDINGS_H

#ifdef __cplusplus
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#define RENDERLAB_BDPT_CONSTANT inline constexpr uint32_t
#define RENDERLAB_BDPT_ALIGN alignas(16)
#define RENDERLAB_BDPT_UINT uint32_t
#define RENDERLAB_BDPT_UINT2 glm::uvec2
#define RENDERLAB_BDPT_FLOAT3 glm::vec3
#else
#define RENDERLAB_BDPT_CONSTANT static const uint
#define RENDERLAB_BDPT_ALIGN
#define RENDERLAB_BDPT_UINT uint
#define RENDERLAB_BDPT_UINT2 uint2
#define RENDERLAB_BDPT_FLOAT3 float3
#endif

namespace BdptBinding
{
RENDERLAB_BDPT_CONSTANT kLightTracing   = 1u << 0;
RENDERLAB_BDPT_CONSTANT kPowerHeuristic = 1u << 1;

struct RENDERLAB_BDPT_ALIGN AccumulatedPixel
{
    RENDERLAB_BDPT_FLOAT3 radiance;
    RENDERLAB_BDPT_UINT   sampleCount;
};

struct RENDERLAB_BDPT_ALIGN ResolveParameters
{
    RENDERLAB_BDPT_UINT2 viewportOffset;
    RENDERLAB_BDPT_UINT2 viewportExtent;
    float                lightSplatScale;
    float                padding0;
    float                padding1;
    float                padding2;
};

#ifdef __cplusplus
static_assert(sizeof(AccumulatedPixel) == 16);
static_assert(sizeof(ResolveParameters) == 32);
#endif
}  // namespace BdptBinding

#undef RENDERLAB_BDPT_CONSTANT
#undef RENDERLAB_BDPT_ALIGN
#undef RENDERLAB_BDPT_UINT
#undef RENDERLAB_BDPT_UINT2
#undef RENDERLAB_BDPT_FLOAT3
#endif
