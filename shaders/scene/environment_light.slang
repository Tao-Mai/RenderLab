#ifndef RENDERLAB_ENVIRONMENT_LIGHT_SLANG
#define RENDERLAB_ENVIRONMENT_LIGHT_SLANG

#include "material/texture_data.slang"
#include "math/constant.slang"

#if HAS_ENV_EQUAL_AREA
struct EnvironmentLightData
{
    float4x4 lightFromWorld;
    float4 emissionScale;
    TextureRef image;
    uint padding0, padding1;

    property emission : float3 { get { return emissionScale.rgb; } }
};
StructuredBuffer<EnvironmentLightData> environmentLight;

// PBRT's equal-area square projection.
float2 equalAreaUv(float3 direction)
{
    float3 d = normalize(direction);
    float2 a = abs(d.xy);
    float radius = sqrt(max(0.0, 1.0 - abs(d.z)));
    float phi = max(a.x, a.y) > 0.0 ? atan2(a.y, a.x) * (2.0 / PI) : 0.0;
    float2 uv = radius * float2(1.0 - phi, phi);
    if (d.z < 0.0)
        uv = 1.0 - uv.yx;
    uv *= float2(d.x < 0.0 ? -1.0 : 1.0, d.y < 0.0 ? -1.0 : 1.0);
    return uv * 0.5 + 0.5;
}
#endif

#if HAS_ENV_CUBEMAP
TextureCube<float4> radianceMap;
SamplerState radianceSampler;
#endif

float3 environmentRadiance(float3 direction)
{
    float3 radiance = float3(0.0);
#if HAS_ENV_EQUAL_AREA
    EnvironmentLightData light = environmentLight[0];
    float3 local = mul(light.lightFromWorld, float4(direction, 0.0)).xyz;
    radiance = light.emission * sampleTextureLevel(light.image, equalAreaUv(local)).rgb;
#endif
#if HAS_ENV_CUBEMAP
    radiance += radianceMap.SampleLevel(radianceSampler, direction, 0.0).rgb;
#endif
    return radiance;
}

float3 sampleEnvironmentDirection(float2 uv)
{
    float elevation = 1.0 - 2.0 * uv.x;
    float radius = sqrt(max(0.0, 1.0 - elevation * elevation));
    float angle = 2.0 * PI * uv.y;
    return float3(radius * cos(angle), elevation, radius * sin(angle));
}

#endif
