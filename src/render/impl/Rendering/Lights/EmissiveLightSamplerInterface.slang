#ifndef RENDERLAB_EMISSIVE_LIGHT_SAMPLER_INTERFACE_SLANG
#define RENDERLAB_EMISSIVE_LIGHT_SAMPLER_INTERFACE_SLANG

#include "Utils/Sampling/SampleGeneratorInterface.slang"

struct TriangleLightSample
{
    uint   triangleIndex;  // Index of the sampled triangle.
    float3 posW;           // Sampled point on the light source in world space.
    float3 normalW;        // Normal of the sampled point on the light source in world space.
    float3 dir;            // Normalized direction from the shading point to the sampled point in world space.
    float  distance;       // Distance from the shading point to the sampled point.
    float3 Le;             // Emitted radiance. Zero if back-facing or sample is invalid.
    float  pdf;            // Probability density with respect to solid angle from the shading point.
};

struct TriangleLightHit
{
    uint   triangleIndex;  // Index of the triangle.
    float3 posW;           // Sampled point on the triangle in world space.
    float3 normalW;        // Face normal of the triangle in world space, flipped for back-facing hits.
};

interface IEmissiveLightSampler
{
    bool sampleLight<S : ISampleGenerator>(
        const float3 posW, const float3 normalW, const bool upperHemisphere, inout S sg, out TriangleLightSample ls);

    float evalTriangleSelectionPdf(
        const float3 posW, const float3 normalW, const bool upperHemisphere, const uint triangleIndex);

    float evalPdf(
        const float3 posW, const float3 normalW, const bool upperHemisphere, const TriangleLightHit hit);
};

#endif
