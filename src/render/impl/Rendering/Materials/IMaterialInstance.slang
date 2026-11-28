#ifndef RENDERLAB_IMATERIAL_INSTANCE_SLANG
#define RENDERLAB_IMATERIAL_INSTANCE_SLANG

#include "Rendering/Materials/LobeType.slang"
#include "Scene/Material/ShadingUtils.slang"
#include "Scene/ShadingData.slang"
#include "Utils/Sampling/SampleGeneratorInterface.slang"

struct BSDFSample
{
    float3 wo;        // Sampled direction in world space (normalized).
    float  pdf;       // PDF with respect to solid angle for wo (0 for delta events).
    float3 weight;    // Sample weight f(wi, wo) * abs(dot(wo, n)) / pdf(wo).
    uint   lobeType;  // Sampled lobeType (LobeType flags).

    bool isLobe(LobeType type) { return (lobeType & uint(type)) != 0; }
};

struct BSDFProperties
{
    float3 emission;     // Radiance emitted in the incident direction (wi).
    float  roughness;    // Surface roughness in [0, 1].
    float3 guideNormal;  // Representative shading normal in world space.
    uint   flags;

    enum Flags : uint
    {
        IsTransmissive = 0x1,
    };

    property bool isTransmissive
    {
        get { return (flags & uint(Flags::IsTransmissive)) != 0; }
    }
};

interface IMaterialInstance
{
    float3 eval<S : ISampleGenerator>(const ShadingData sd, const float3 wo, inout S sg);
    bool sample<S : ISampleGenerator>(const ShadingData sd, inout S sg, out BSDFSample result);
    float evalPdf(const ShadingData sd, const float3 wo);
    BSDFProperties getProperties(const ShadingData sd);
    uint getLobeTypes(const ShadingData sd);
};

#endif
