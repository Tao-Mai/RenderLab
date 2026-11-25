#ifndef RENDERLAB_ENVIRONMENT_LIGHT_SLANG
#define RENDERLAB_ENVIRONMENT_LIGHT_SLANG

#include "render/impl/math/constant.slang"

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

float3 sampleEnvironmentDirection(float2 uv)
{
    float elevation = 1.0 - 2.0 * uv.x;
    float radius = sqrt(max(0.0, 1.0 - elevation * elevation));
    float angle = 2.0 * PI * uv.y;
    return float3(radius * cos(angle), elevation, radius * sin(angle));
}

#endif
