#ifndef RENDERLAB_OFFLINE_LIGHT_SLANG
#define RENDERLAB_OFFLINE_LIGHT_SLANG

#include "raytracing/offline/texture.slang"

struct LightEmission
{
    float4 colorScale; // RGB preview, scale.
    float4 normalization; // Source power in lm, illuminance in lx, total source area, reserved.
    uint4 resources; // Texture, sampler, medium, flags.

    property rgb : float3 {
        get { return colorScale.rgb; }
    }

    property scale : float {
        get { return colorScale.w; }
    }

    property textureIndex : uint {
        get { return resources.x; }
    }

    property samplerIndex : uint {
        get { return resources.y; }
    }

    property mediumIndex : uint {
        get { return resources.z; }
    }

    property flags : uint {
        get { return resources.w; }
    }

    property hasImage : bool {
        get { return (flags & kOfflineLightHasImage) != 0; }
    }
};

struct PointLight
{
    float3 position;
    uint padding;
    LightEmission emission;
};

struct SpotLight
{
    float3 position;
    float cosInner;
    float3 direction;
    float cosOuter;
    LightEmission emission;
};

struct DirectionalLight
{
    float3 direction;
    uint padding;
    LightEmission emission;
};

struct LightTriangle
{
    float4 p0Area;
    float4 p1;
    float4 p2;
    float4 normal;
    float4 uv01;
    float2 uv2;
    uint emissionIndex;
    uint twoSided;

    property p0 : float3 {
        get { return p0Area.xyz; }
    }

    property area : float {
        get { return p0Area.w; }
    }

    property geometricNormal : float3 {
        get { return normal.xyz; }
    }
};

struct EnvironmentLight
{
    float4x4 lightFromWorld;
    uint firstPortal;
    uint portalCount;
    uint padding0, padding1;
    LightEmission emission;
};

struct ProjectionLight
{
    float4x4 lightFromWorld;
    float3 position;
    float fieldOfView;
    LightEmission emission;
};

struct GoniometricLight
{
    float4x4 lightFromWorld;
    float3 position;
    uint padding;
    LightEmission emission;
};

[[vk::binding(OfflineRayTracingBinding::kAreaEmissions, 3)]] StructuredBuffer<LightEmission> areaEmissions;
[[vk::binding(OfflineRayTracingBinding::kPoints, 3)]] StructuredBuffer<PointLight> pointLights;
[[vk::binding(OfflineRayTracingBinding::kSpots, 3)]] StructuredBuffer<SpotLight> spotLights;
[[vk::binding(OfflineRayTracingBinding::kDirectionals, 3)]] StructuredBuffer<DirectionalLight> directionalLights;
[[vk::binding(OfflineRayTracingBinding::kTriangles, 3)]] StructuredBuffer<LightTriangle> lightTriangles;
[[vk::binding(OfflineRayTracingBinding::kEnvironments, 3)]] StructuredBuffer<EnvironmentLight> environmentLight;
[[vk::binding(OfflineRayTracingBinding::kProjections, 3)]] StructuredBuffer<ProjectionLight> projectionLights;
[[vk::binding(OfflineRayTracingBinding::kGoniometrics, 3)]] StructuredBuffer<GoniometricLight> goniometricLights;
[[vk::binding(OfflineRayTracingBinding::kPortals, 3)]] StructuredBuffer<float4> lightPortals;

// PBRT-v4's equal-area square projection, with an exact atan instead of its polynomial approximation.
float2 equalAreaUv(float3 direction)
{
    float3 d = normalize(direction);
    float2 a = abs(d.xy);
    float radius = sqrt(max(0.0, 1.0 - abs(d.z)));
    float phi = max(a.x, a.y) > 0.0 ? atan2(a.y, a.x) * (2.0 / PI) : 0.0;
    float2 uv = radius * float2(1.0 - phi, phi);
    if (d.z < 0.0)
    {
        uv = 1.0 - uv.yx;
    }
    uv *= float2(d.x < 0.0 ? -1.0 : 1.0, d.y < 0.0 ? -1.0 : 1.0);

    return uv * 0.5 + 0.5;
}

float3 emissionRgb(LightEmission source, float2 imageUv)
{
    float3 emission = source.rgb;
    if (source.hasImage)
    {
        TextureRef image = {source.textureIndex, source.samplerIndex};
        emission = sampleTextureLevel(image, imageUv).rgb;
    }

    return emission * source.scale;
}

float3 areaEmission(uint lightIndex, float2 uv, float3 outgoing)
{
    LightTriangle triangle = lightTriangles[lightIndex];
    if (triangle.area == 0.0)
    {
        return float3(0.0);
    }
    if (triangle.twoSided == 0 && dot(triangle.geometricNormal, outgoing) <= 0.0)
    {
        return float3(0.0);
    }

    return emissionRgb(areaEmissions[triangle.emissionIndex], float2(uv.x, 1.0 - uv.y));
}

float3 environmentEmission(bool enabled, float3 direction)
{
    if (!enabled)
    {
        return float3(0.0);
    }

    EnvironmentLight light = environmentLight[0];
    float3 local = mul(light.lightFromWorld, float4(direction, 0.0)).xyz;

    return emissionRgb(light.emission, equalAreaUv(local));
}

#endif
