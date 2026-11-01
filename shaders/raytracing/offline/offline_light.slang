#ifndef RENDERLAB_OFFLINE_LIGHT_SLANG
#define RENDERLAB_OFFLINE_LIGHT_SLANG

#include "raytracing/offline/offline_texture.slang"

struct OfflineLightEmission
{
    float4 colorScale;
    float4 normalization;
    uint4 resources;
};

struct OfflinePointLight
{
    float3 position;
    uint padding;
    OfflineLightEmission emission;
};

struct OfflineSpotLight
{
    float3 position;
    float cosInner;
    float3 direction;
    float cosOuter;
    OfflineLightEmission emission;
};

struct OfflineDirectionalLight
{
    float3 direction;
    uint padding;
    OfflineLightEmission emission;
};

struct OfflineLightTriangle
{
    float4 p0Area;
    float4 p1;
    float4 p2;
    float4 normal;
    float4 uv01;
    float2 uv2;
    uint emissionIndex;
    uint twoSided;
};

struct OfflineEnvironmentLight
{
    float4x4 lightFromWorld;
    uint firstPortal;
    uint portalCount;
    uint padding0, padding1;
    OfflineLightEmission emission;
};

struct OfflineProjectionLight
{
    float4x4 lightFromWorld;
    float3 position;
    float fieldOfView;
    OfflineLightEmission emission;
};

struct OfflineGoniometricLight
{
    float4x4 lightFromWorld;
    float3 position;
    uint padding;
    OfflineLightEmission emission;
};

[[vk::binding(OfflineRayTracingBinding::kAreaEmissions, 3)]] StructuredBuffer<OfflineLightEmission> areaEmissions;
[[vk::binding(OfflineRayTracingBinding::kPoints, 3)]] StructuredBuffer<OfflinePointLight> pointLights;
[[vk::binding(OfflineRayTracingBinding::kSpots, 3)]] StructuredBuffer<OfflineSpotLight> spotLights;
[[vk::binding(OfflineRayTracingBinding::kDirectionals, 3)]] StructuredBuffer<OfflineDirectionalLight> directionalLights;
[[vk::binding(OfflineRayTracingBinding::kTriangles, 3)]] StructuredBuffer<OfflineLightTriangle> lightTriangles;
[[vk::binding(OfflineRayTracingBinding::kEnvironments, 3)]] StructuredBuffer<OfflineEnvironmentLight> environmentLights;
[[vk::binding(OfflineRayTracingBinding::kProjections, 3)]] StructuredBuffer<OfflineProjectionLight> projectionLights;
[[vk::binding(OfflineRayTracingBinding::kGoniometrics, 3)]] StructuredBuffer<OfflineGoniometricLight> goniometricLights;
[[vk::binding(OfflineRayTracingBinding::kPortals, 3)]] StructuredBuffer<float4> lightPortals;

// PBRT-v4's equal-area square projection, with an exact atan instead of its polynomial approximation.
float2 offlineEqualAreaUv(float3 direction)
{
    float3 d = normalize(direction);
    float2 a = abs(d.xy);
    float radius = sqrt(max(0.0, 1.0 - abs(d.z)));
    float phi = max(a.x, a.y) > 0.0 ? atan2(a.y, a.x) * (2.0 / kOfflinePi) : 0.0;
    float2 uv = radius * float2(1.0 - phi, phi);
    if (d.z < 0.0)
    {
        uv = 1.0 - uv.yx;
    }
    uv *= float2(d.x < 0.0 ? -1.0 : 1.0, d.y < 0.0 ? -1.0 : 1.0);
    return uv * 0.5 + 0.5;
}

float3 offlineEmissionRgb(OfflineLightEmission source, float2 imageUv)
{
    float3 emission = source.colorScale.rgb;
    if ((source.resources.w & kOfflineLightHasImage) != 0)
    {
        TextureRef image = {source.resources.x, source.resources.y};
        emission = sampleTextureLevel(image, imageUv).rgb;
    }
    return emission * source.colorScale.w;
}

float3 offlineAreaEmission(uint lightIndex, float2 uv, float3 outgoing)
{
    OfflineLightTriangle triangle = lightTriangles[lightIndex];
    if (triangle.p0Area.w == 0.0)
    {
        return float3(0.0);
    }
    if (triangle.twoSided == 0 && dot(triangle.normal.xyz, outgoing) <= 0.0)
    {
        return float3(0.0);
    }
    return offlineEmissionRgb(areaEmissions[triangle.emissionIndex], float2(uv.x, 1.0 - uv.y));
}

float3 offlineEnvironmentEmission(uint lightCount, float3 direction)
{
    float3 emission = float3(0.0);
    for (uint index = 0; index < lightCount; ++index)
    {
        OfflineEnvironmentLight light = environmentLights[index];
        float3 local = mul(light.lightFromWorld, float4(direction, 0.0)).xyz;
        emission += offlineEmissionRgb(light.emission, offlineEqualAreaUv(local));
    }
    return emission;
}

#endif
