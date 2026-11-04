#ifndef RENDERLAB_REALTIME_TRIANGLE_LIGHT_SLANG
#define RENDERLAB_REALTIME_TRIANGLE_LIGHT_SLANG

#include "common/material_data.slang"
#include "raytracing/realtime/geometry.slang"

float3 triangleBarycentrics(float2 uv)
{
    float root = sqrt(uv.x);
    return float3(1.0 - root, root * (1.0 - uv.y), root * uv.y);
}

float2 triangleSampleUv(float2 barycentrics)
{
    float root = barycentrics.x + barycentrics.y;
    return float2(root * root, root > 0.0 ? barycentrics.y / root : 0.0);
}

float3 triangleLightEmission(EmissiveTriangle light, float3 barycentrics)
{
    MaterialData material = materials[light.materialIndex];
    float2 uv = light.uv0 * barycentrics.x + light.uv1 * barycentrics.y + light.uv2 * barycentrics.z;
    return material.emission * sampleTextureLevel(material.emissiveTexture, uv).rgb;
}

#endif
