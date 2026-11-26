#ifndef RENDERLAB_ENV_MAP_SLANG
#define RENDERLAB_ENV_MAP_SLANG

#include "math/constant.slang"

#ifndef HAS_ENV
#error HAS_ENV must be specified when compiling ray tracing environment lighting
#endif

#if HAS_ENV
TextureCube<float4> radianceMap;
SamplerState radianceSampler;
#endif

float3 environmentRadiance(float3 direction)
{
#if HAS_ENV
    return radianceMap.SampleLevel(radianceSampler, direction, 0.0).rgb;
#else
    return float3(0.0);
#endif
}

#endif
