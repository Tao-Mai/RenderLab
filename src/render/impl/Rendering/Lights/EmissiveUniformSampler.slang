#ifndef RENDERLAB_EMISSIVE_UNIFORM_SAMPLER_SLANG
#define RENDERLAB_EMISSIVE_UNIFORM_SAMPLER_SLANG

#include "Rendering/Lights/EmissiveLightSamplerInterface.slang"
#include "Rendering/Lights/EmissiveLightSamplerHelpers.slang"
#include "math/random.slang"
#include "raytracing/alpha_test.slang"

struct EmissiveUniformSampler : IEmissiveLightSampler
{
    float evalTriangleSelectionPdf(uint triangleIndex)
    {
        return 1.0 / float(emissiveLights.getTriangleCount());
    }

    float evalPdf(float3 origin, TriangleLightHit hit)
    {
        EmissiveTriangle triangle = emissiveLights.triangles[hit.triangleIndex];
        if (triangle.area <= 0.0)
            return 0.0;

        float3 offset = hit.position - origin;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0)
            return 0.0;

        float3 direction = offset / sqrt(distanceSquared);
        float cosine = dot(triangle.geometricNormal, -direction);
        cosine = max(0.0, cosine);
        if (cosine <= 0.0)
            return 0.0;

        return evalTriangleSelectionPdf(hit.triangleIndex) * distanceSquared / (triangle.area * cosine);
    }

    bool sampleLight<S : ISampleGenerator>(float3 origin, inout S generator, out TriangleLightSample result)
    {
        result = {};
        const uint lightCount = emissiveLights.getTriangleCount();
        if (lightCount == 0)
            return false;

        result.triangleIndex = randomIndex(lightCount, generator);
        float2 sampleValue = sampleNext2D(generator);

        float2 uv;
        result.position = sampleEmissiveTriangle(result.triangleIndex, sampleValue, uv);
        const TriangleLightHit hit = {result.triangleIndex, result.position};
        result.pdf = evalPdf(origin, hit);
        if (result.pdf <= 0.0)
            return false;

        EmissiveTriangle triangle = emissiveLights.triangles[result.triangleIndex];
        StandardMaterialData material = materials[triangle.materialIndex];
        if (material.alphaMode == kAlphaModeMask && failsAlphaCutoff(material, uv))
            return false;

        float3 offset = result.position - origin;
        result.distance = length(offset);
        result.direction = offset / result.distance;
        result.normal = triangle.geometricNormal;
        result.radiance = emissiveRadiance(result.triangleIndex, uv, -result.direction);

        return true;
    }
};

#endif
