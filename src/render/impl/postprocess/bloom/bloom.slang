#include "render/impl/postprocess/fullscreen.slang"

#ifndef BLOOM_STAGE
#error BLOOM_STAGE must be specified
#endif

struct BloomParameters
{
    float2 texelSize;
    float filterRadius;
    float intensity;
    float scatter;
};
ConstantBuffer<BloomParameters> parameters;
Texture2D<float4> sourceColor;
SamplerState linearSampler;
Texture2D<float4> baseColor;

float3 sampleSource(float2 uv)
{
    return max(sourceColor.SampleLevel(linearSampler, uv, 0.0).rgb, float3(0.0));
}

// Separable 9x9 Gaussian used by Catlike Coding's bloom pyramid.
// The horizontal draw also halves the resolution; the vertical draw keeps it.
float3 downsampleGaussian(float2 uv, float2 step)
{
    static const float kWeights[9] = {
        66.0 / 4070.0, 220.0 / 4070.0, 495.0 / 4070.0, 792.0 / 4070.0, 924.0 / 4070.0,
        792.0 / 4070.0, 495.0 / 4070.0, 220.0 / 4070.0, 66.0 / 4070.0,
    };
    float3 result = float3(0.0);
    [unroll]
    for (int tap = 0; tap < 9; ++tap)
        result += sampleSource(uv + float(tap - 4) * step * parameters.filterRadius) * kWeights[tap];
    return result;
}

[shader("fragment")]
float4 fragMain(FullscreenVertexOutput input) : SV_Target
{
#if BLOOM_STAGE == 0
    float3 color = downsampleGaussian(input.uv, float2(2.0 * parameters.texelSize.x, 0.0));
    return float4(color, 1.0);
#elif BLOOM_STAGE == 1
    float3 color = downsampleGaussian(input.uv, float2(0.0, parameters.texelSize.y));
    return float4(color, 1.0);
#elif BLOOM_STAGE == 2
    float3 lowRes = sampleSource(input.uv);
    float3 highRes = baseColor.SampleLevel(linearSampler, input.uv, 0.0).rgb;
    return float4(lerp(highRes, lowRes, parameters.scatter), 1.0);
#elif BLOOM_STAGE == 3
    float3 scene = sourceColor.SampleLevel(linearSampler, input.uv, 0.0).rgb;
    float3 bloom = baseColor.SampleLevel(linearSampler, input.uv, 0.0).rgb;
    // No threshold: scatter all incoming light, then mix it back without additive energy gain.
    return float4(lerp(scene, bloom, parameters.intensity), 1.0);
#else
#error Unsupported BLOOM_STAGE
#endif
}
