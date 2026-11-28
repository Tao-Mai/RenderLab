#ifndef RENDERLAB_EMISSIVE_UNIFORM_SAMPLER_SLANG
#define RENDERLAB_EMISSIVE_UNIFORM_SAMPLER_SLANG

#include "Rendering/Lights/EmissiveLightSamplerInterface.slang"
#include "Rendering/Lights/EmissiveLightSamplerHelpers.slang"

struct EmissiveUniformSampler : IEmissiveLightSampler
{
    bool sampleLight<S : ISampleGenerator>(
        const float3 posW, const float3 normalW, const bool upperHemisphere, inout S sg, out TriangleLightSample ls)
    {
        ls = {};
        const uint triangleCount = gScene.lightCollection.getTriangleCount();
        if (triangleCount == 0)
            return false;

        const float uLight = sampleNext1D(sg);
        const uint triangleIndex = min(uint(uLight * triangleCount), triangleCount - 1u);
        const float triangleSelectionPdf = 1.f / float(triangleCount);
        const float2 u = sampleNext2D(sg);

        if (!sampleTriangle(posW, triangleIndex, u, ls))
            return false;

        ls.pdf *= triangleSelectionPdf;

        return true;
    }

    float evalTriangleSelectionPdf(
        const float3 posW, const float3 normalW, const bool upperHemisphere, const uint triangleIndex)
    {
        if (triangleIndex == LightCollection::kInvalidIndex)
            return 0.f;

        const uint triangleCount = gScene.lightCollection.getTriangleCount();

        return triangleCount > 0 ? 1.f / float(triangleCount) : 0.f;
    }

    float evalPdf(
        const float3 posW, const float3 normalW, const bool upperHemisphere, const TriangleLightHit hit)
    {
        if (hit.triangleIndex == LightCollection::kInvalidIndex)
            return 0.f;

        const float triangleSelectionPdf = evalTriangleSelectionPdf(posW, normalW, upperHemisphere, hit.triangleIndex);
        const float trianglePdf = evalTrianglePdf(posW, hit);

        return triangleSelectionPdf * trianglePdf;
    }
};

#endif
