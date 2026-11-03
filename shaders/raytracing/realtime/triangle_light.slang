#ifndef RENDERLAB_REALTIME_TRIANGLE_LIGHT_SLANG
#define RENDERLAB_REALTIME_TRIANGLE_LIGHT_SLANG

#include "common/material_data.slang"
#include "common/random.slang"
#include "raytracing/realtime/geometry.slang"

uint sampleEmissiveTriangleIndex(uint count, inout uint state)
{
    // Rejection removes modulo bias while keeping every triangle equally likely.
    uint threshold = (0u - count) % count;
    uint value;
    do
    {
        value = randomUint(state);
    }
    while (value < threshold);
    return value % count;
}

float3 sampleTriangleBarycentrics(inout uint state)
{
    float root = sqrt(randomFloat(state));
    float second = randomFloat(state);
    return float3(1.0 - root, root * (1.0 - second), root * second);
}

float triangleLightPdf(EmissiveTriangle light, float3 origin, float3 target, uint triangleCount)
{
    if (light.area <= 0.0)
    {
        return 0.0;
    }
    float3 offset = origin - target;
    float distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0)
    {
        return 0.0;
    }

    float3 normal = cross(light.edge1, light.edge2) / (2.0 * light.area);
    float cosine = dot(normal, offset * rsqrt(distanceSquared));
    if (materials[light.materialIndex].doubleSided != 0)
    {
        cosine = abs(cosine);
    }
    if (cosine <= 0.0)
    {
        return 0.0;
    }
    return distanceSquared / (float(triangleCount) * light.area * cosine);
}

float3 triangleLightEmission(EmissiveTriangle light, float3 barycentrics)
{
    MaterialData material = materials[light.materialIndex];
    float2 uv = light.uv0 * barycentrics.x + light.uv1 * barycentrics.y + light.uv2 * barycentrics.z;
    return material.emission * sampleTextureLevel(material.emissiveTexture, uv).rgb;
}

#endif
