#ifndef BLOOM_STAGE
#error BLOOM_STAGE must be specified
#endif

struct BloomParameters
{
    float4 viewport;
    float2 texelSize;
    float filterRadius;
    float intensity;
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

float3 downsample(float2 uv)
{
    float2 step = parameters.texelSize;
    float3 result = sampleSource(uv) * 0.125;
    // Normalized 13-tap low-pass filter; the four inner taps carry half the weight.
    for (int y = -1; y <= 1; y += 2)
        for (int x = -1; x <= 1; x += 2)
        {
            result += sampleSource(uv + float2(x, y) * step) * 0.125;
            result += sampleSource(uv + float2(x, y) * step * 2.0) * 0.03125;
        }
    result += (sampleSource(uv + float2(2, 0) * step) + sampleSource(uv - float2(2, 0) * step)
             + sampleSource(uv + float2(0, 2) * step) + sampleSource(uv - float2(0, 2) * step)) * 0.0625;
    return result;
}

float3 upsample(float2 uv)
{
    float2 step = parameters.texelSize * parameters.filterRadius;
    float3 result = float3(0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
            result += sampleSource(uv + float2(x, y) * step) * weight;
        }
    // Normalize each reconstruction level so uniform HDR input retains its brightness.
    return (baseColor.SampleLevel(linearSampler, uv, 0.0).rgb + result / 16.0) * 0.5;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    bool insideViewport = all(input.uv >= parameters.viewport.xy) && all(input.uv < parameters.viewport.zw);
#if BLOOM_STAGE == 0
    return float4(insideViewport ? downsample(input.uv) : float3(0.0), 1.0);
#elif BLOOM_STAGE == 1
    return float4(insideViewport ? upsample(input.uv) : float3(0.0), 1.0);
#elif BLOOM_STAGE == 2
    float3 scene = sourceColor.SampleLevel(linearSampler, input.uv, 0.0).rgb;
    float3 bloom = baseColor.SampleLevel(linearSampler, input.uv, 0.0).rgb;
    return float4(insideViewport ? lerp(scene, bloom, parameters.intensity) : scene, 1.0);
#else
#error Unsupported BLOOM_STAGE
#endif
}
