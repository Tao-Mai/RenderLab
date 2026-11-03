#include "common/material_evaluation.slang"
#include "common/random.slang"

[[vk::binding(0, 0)]] RWStructuredBuffer<float4> results;
[[vk::binding(1, 0)]] StructuredBuffer<MaterialData> cases;

struct ConstantSampler : IMaterialTextureSampler
{
    float4 sample(TextureRef texture, float2 uv)
    {
        if (texture.textureIndex == 1)
        {
            return float4(0.25, 0.5, 0.75, 0.4);
        }
        if (texture.textureIndex == 2)
        {
            return float4(0.2, 0.4, 0.6, 0.5);
        }
        return float4(1.0);
    }
};

[shader("compute")]
[numthreads(8, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint index = thread.x;
    PbrBrdf brdf = evaluatePbrMaterial(cases[index], float2(0.0), ConstantSampler()).brdf;
    float3 normal = float3(0.0, 0.0, 1.0);
    float3 view = float3(0.6, 0.0, 0.8);
    float3 sampledIntegral = float3(0.0);
    float3 referenceIntegral = float3(0.0);
    float maximumPdfError = 0.0;
    uint state = 0x17283940u;
    static const uint kSampleCount = 16384;
    for (uint sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex)
    {
        float3 randomSample = float3(randomFloat(state), randomFloat(state), randomFloat(state));
        BrdfSample selected = brdf.sample(normal, view, randomSample);
        sampledIntegral += selected.weight;
        if (selected.pdf > 0.0)
        {
            maximumPdfError = max(maximumPdfError, abs(selected.pdf - brdf.pdf(normal, view, selected.direction)));
        }

        float cosine = (float(sampleIndex) + 0.5) / float(kSampleCount);
        float phi = 2.0 * PI * float(reversebits(sampleIndex)) * (1.0 / 4294967296.0);
        float radius = sqrt(1.0 - cosine * cosine);
        float3 direction = float3(radius * cos(phi), radius * sin(phi), cosine);
        referenceIntegral += brdf.evaluate(normal, view, direction) * cosine * (2.0 * PI);
    }

    uint output = index * 5;
    results[output] = float4(brdf.diffuseColor, brdf.roughness);
    results[output + 1] = float4(brdf.specularF0, brdf.specularF90);
    results[output + 2] = float4(brdf.evaluate(normal, normal, normal), brdf.diffuseFresnelWeight);
    results[output + 3] = float4(sampledIntegral / kSampleCount, maximumPdfError);
    results[output + 4] = float4(referenceIntegral / kSampleCount, brdf.specularProbability(0.8));
}
