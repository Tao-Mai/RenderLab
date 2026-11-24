#ifndef RENDERLAB_LIGHT_EMITTERS_SLANG
#define RENDERLAB_LIGHT_EMITTERS_SLANG


#include "material/texture_data.slang"
#include "math/constant.slang"
#include "render/resources/SceneLightConstants.h"
#include "scene/light_data.slang"

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
        get { return (flags & kLightHasImage) != 0; }
    }

    property flipUv : bool {
        get { return (flags & kLightFlipUv) != 0; }
    }

    property multiplyColor : bool {
        get { return (flags & kLightMultiplyColor) != 0; }
    }
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

    property vertex1 : float3 {
        get { return p1.xyz; }
    }

    property vertex2 : float3 {
        get { return p2.xyz; }
    }

    property area : float {
        get { return p0Area.w; }
    }

    property geometricNormal : float3 {
        get { return normal.xyz; }
    }

    property uv0 : float2 {
        get { return uv01.xy; }
    }

    property uv1 : float2 {
        get { return uv01.zw; }
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

struct LightTriangleHeader
{
    uint triangleCount;
    uint firstRectTriangle;
    uint firstRectLight;
    uint padding;
};

StructuredBuffer<LightData> lights;
StructuredBuffer<LightEmission> areaEmissions;
ByteAddressBuffer lightTriangles;
#if HAS_ENV_EQUAL_AREA
StructuredBuffer<EnvironmentLight> environmentLight;
#endif
#if HAS_ENV_CUBEMAP
TextureCube<float4> radianceMap;
SamplerState radianceSampler;
#endif

LightTriangle loadTriangleLight(uint index)
{
    return lightTriangles.Load<LightTriangle>(kLightTriangleHeaderSize + index * kLightTriangleStride);
}

uint triangleLightCount()
{
    return lightTriangles.Load(0);
}

uint rectTriangleIndex(uint lightIndex, uint primitiveIndex)
{
    LightTriangleHeader header = lightTriangles.Load<LightTriangleHeader>(0);
    return header.firstRectTriangle + (lightIndex - header.firstRectLight) * 2 + primitiveIndex;
}

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
        if (source.flipUv)
            imageUv.y = 1.0 - imageUv.y;
        float3 texel = sampleTextureLevel(image, imageUv).rgb;
        emission = source.multiplyColor ? emission * texel : texel;
    }

    return emission * source.scale;
}

float3 averageEmission(LightEmission source)
{
    return (source.hasImage && !source.multiplyColor ? float3(1.0) : source.rgb) * source.scale;
}

float3 areaEmission(uint lightIndex, float2 uv, float3 outgoing)
{
    LightTriangle triangle = loadTriangleLight(lightIndex);
    if (triangle.area == 0.0)
    {
        return float3(0.0);
    }
    if (triangle.twoSided == 0 && dot(triangle.geometricNormal, outgoing) <= 0.0)
    {
        return float3(0.0);
    }

    return emissionRgb(areaEmissions[triangle.emissionIndex], uv);
}

float3 environmentRadiance(float3 direction)
{
    float3 radiance = float3(0.0);
#if HAS_ENV_EQUAL_AREA
    EnvironmentLight light = environmentLight[0];
    float3 local = mul(light.lightFromWorld, float4(direction, 0.0)).xyz;
    radiance = emissionRgb(light.emission, equalAreaUv(local));
#endif
#if HAS_ENV_CUBEMAP
    radiance += radianceMap.SampleLevel(radianceSampler, direction, 0.0).rgb;
#endif

    return radiance;
}

float3 triangleBarycentrics(float2 sampleValue)
{
    float root = sqrt(sampleValue.x);
    return float3(1.0 - root, root * (1.0 - sampleValue.y), root * sampleValue.y);
}

float2 triangleUv(LightTriangle triangle, float3 barycentrics)
{
    return triangle.uv0 * barycentrics.x + triangle.uv1 * barycentrics.y + triangle.uv2 * barycentrics.z;
}

float3 sampleTriangle(uint lightIndex, float2 sampleValue, out float2 uv)
{
    LightTriangle triangle = loadTriangleLight(lightIndex);
    float3 barycentrics = triangleBarycentrics(sampleValue);
    uv = triangleUv(triangle, barycentrics);
    return triangle.p0 * barycentrics.x + triangle.vertex1 * barycentrics.y + triangle.vertex2 * barycentrics.z;
}

#endif
