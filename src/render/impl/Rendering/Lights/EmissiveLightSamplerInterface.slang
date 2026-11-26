#ifndef RENDERLAB_EMISSIVE_LIGHT_SAMPLER_INTERFACE_SLANG
#define RENDERLAB_EMISSIVE_LIGHT_SAMPLER_INTERFACE_SLANG

#include "Utils/Sampling/SampleGeneratorInterface.slang"

struct TriangleLightSample
{
    uint   triangleIndex;
    float3 position;
    float3 normal;
    float3 direction;
    float  distance;
    float3 radiance;  // Emitted radiance, before division by the PDF.
    float  pdf;       // Solid-angle PDF including triangle selection.
};

struct TriangleLightHit
{
    uint   triangleIndex;
    float3 position;
};

interface IEmissiveLightSampler
{
    bool sampleLight<S : ISampleGenerator>(float3 origin, inout S generator, out TriangleLightSample result);
    float evalTriangleSelectionPdf(uint triangleIndex);
    float evalPdf(float3 origin, TriangleLightHit hit);
};

#endif
