#ifndef RENDERLAB_OFFLINE_BRDF_SLANG
#define RENDERLAB_OFFLINE_BRDF_SLANG

#include "common/constant.slang"

struct OfflineBrdfSample
{
    float3 direction;
    float3 weight;
    float pdf;
};

struct OfflineDiffuseBrdf
{
    float3 albedo;

    float3 evaluate()
    {
        return albedo * INV_PI;
    }

    float pdf(float3 normal, float3 direction)
    {
        return max(0.0, dot(normal, direction)) * INV_PI;
    }

    OfflineBrdfSample sample(float3 normal, float2 sampleValue)
    {
        float radius = sqrt(sampleValue.x);
        float phi = 2.0 * PI * sampleValue.y;
        float3 local = float3(radius * cos(phi), radius * sin(phi), sqrt(1.0 - sampleValue.x));
        float3 axis = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
        float3 tangent = normalize(cross(axis, normal));
        float3 bitangent = cross(normal, tangent);

        OfflineBrdfSample result;
        result.direction = normalize(tangent * local.x + bitangent * local.y + normal * local.z);
        result.weight = albedo;
        result.pdf = pdf(normal, result.direction);
        return result;
    }
};

#endif
