#ifndef BLOOM_STAGE
#error BLOOM_STAGE must be specified
#endif

struct BloomParameters
{
    float4 viewport;
    float2 texelSize;
    float filterRadius;
    float intensity;
    float scatter;
};
[[vk::push_constant]] ConstantBuffer<BloomParameters> parameters;
[[vk::binding(0, 0)]] Texture2D<float4> sourceColor;
[[vk::binding(1, 0)]] SamplerState linearSampler;
[[vk::binding(2, 0)]] Texture2D<float4> baseColor;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexId : SV_VertexID)
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return {float4(corner * 2.0 - 1.0, 0.0, 1.0), corner};
}

float3 sampleSource(float2 uv)
{
    float2 center = (parameters.viewport.xy + parameters.viewport.zw) * 0.5;
    float2 minimum = min(parameters.viewport.xy + parameters.texelSize * 0.5, center);
    float2 maximum = max(parameters.viewport.zw - parameters.texelSize * 0.5, minimum);
    return max(sourceColor.SampleLevel(linearSampler, clamp(uv, minimum, maximum), 0.0).rgb, float3(0.0));
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
float4 fragMain(VertexOutput input) : SV_Target
{
    bool insideViewport = all(input.uv >= parameters.viewport.xy) && all(input.uv < parameters.viewport.zw);
#if BLOOM_STAGE == 0
    float3 color = downsampleGaussian(input.uv, float2(2.0 * parameters.texelSize.x, 0.0));
    return float4(insideViewport ? color : float3(0.0), 1.0);
#elif BLOOM_STAGE == 1
    float3 color = downsampleGaussian(input.uv, float2(0.0, parameters.texelSize.y));
    return float4(insideViewport ? color : float3(0.0), 1.0);
#elif BLOOM_STAGE == 2
    float3 lowRes = sampleSource(input.uv);
    float3 highRes = baseColor.SampleLevel(linearSampler, input.uv, 0.0).rgb;
    return float4(insideViewport ? lerp(highRes, lowRes, parameters.scatter) : float3(0.0), 1.0);
#elif BLOOM_STAGE == 3
    float3 scene = sourceColor.SampleLevel(linearSampler, input.uv, 0.0).rgb;
    float3 bloom = baseColor.SampleLevel(linearSampler, input.uv, 0.0).rgb;
    // No threshold: scatter all incoming light, then mix it back without additive energy gain.
    return float4(insideViewport ? lerp(scene, bloom, parameters.intensity) : scene, 1.0);
#else
#error Unsupported BLOOM_STAGE
#endif
}
