#include "svgf_common.slang"

[[vk::binding(0, 0)]] Texture2D<float4> inputDirect;   // RGB color, A luminance variance.
[[vk::binding(1, 0)]] Texture2D<float4> inputIndirect;
[[vk::binding(2, 0)]] Texture2D<float> depthImage;
[[vk::binding(3, 0)]] Texture2D<float4> normalImage;
#if SVGF_STAGE == 2
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputColor;
[[vk::binding(5, 0)]] Texture2D<float4> emission;
[[vk::binding(6, 0)]] Texture2D<float4> albedoImage;
#else
[[vk::binding(4, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> outputDirect;
[[vk::binding(5, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> outputIndirect;
#if SVGF_STAGE == 0
[[vk::binding(6, 0)]] [[vk::image_format("rgba32f")]] RWTexture2DArray<float4> historyDirect;
[[vk::binding(7, 0)]] [[vk::image_format("rgba32f")]] RWTexture2DArray<float4> historyIndirect;
#endif
#endif

struct FilterParameters
{
    float4 viewport;
    uint stepSize;
    uint historyLayer;
};
[[vk::push_constant]] ConstantBuffer<FilterParameters> parameters;

static const float kWaveletKernel[3] = {3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0};
static const float kVarianceKernel[2] = {0.5, 0.25};

float2 localVariance(int2 pixel, uint2 extent)
{
    float2 variance = float2(0.0);
    float weightSum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            int2 offset = int2(x, y);
            int2 neighbor = pixel + offset;
            if (!svgfContainsPixel(neighbor, extent, parameters.viewport))
                continue;
            float weight = kVarianceKernel[abs(x)] * kVarianceKernel[abs(y)];
            variance += weight * float2(inputDirect.Load(int3(neighbor, 0)).a, inputIndirect.Load(int3(neighbor, 0)).a);
            weightSum += weight;
        }
    return variance / weightSum;
}

void filter(int2 pixel, uint2 extent, out float4 direct, out float4 indirect)
{
    direct = inputDirect.Load(int3(pixel, 0));
    indirect = inputIndirect.Load(int3(pixel, 0));
    float depth = depthImage.Load(int3(pixel, 0));
    if (depth <= 0.0 || !svgfContainsPixel(pixel, extent, parameters.viewport))
        return;
    float3 normal = normalImage.Load(int3(pixel, 0)).xyz;
    float2 gradient = svgfDepthGradient(depthImage, pixel, depth, extent, parameters.viewport);
    // This Gaussian smoothing only guides the weights; propagate the original variance with squared weights.
    float2 sigma = kSvgfPhiLuminance * sqrt(max(float2(0.0), localVariance(pixel, extent)))
        + kSvgfEpsilon;
    float2 centerLuminance = float2(svgfLuminance(direct.rgb), svgfLuminance(indirect.rgb));
    float4 directSum = float4(0.0);
    float4 indirectSum = float4(0.0);
    float2 weightSum = float2(0.0);
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            int2 offset = int2(x, y) * int(parameters.stepSize);
            int2 neighbor = pixel + offset;
            if (!svgfContainsPixel(neighbor, extent, parameters.viewport))
                continue;
            float geometryWeight = svgfGeometryWeight(depth, depthImage.Load(int3(neighbor, 0)), gradient, offset,
                normal, normalImage.Load(int3(neighbor, 0)).xyz);
            float4 sampleDirect = inputDirect.Load(int3(neighbor, 0));
            float4 sampleIndirect = inputIndirect.Load(int3(neighbor, 0));
            float2 luminance = float2(svgfLuminance(sampleDirect.rgb), svgfLuminance(sampleIndirect.rgb));
            float2 weight = kWaveletKernel[abs(x)] * kWaveletKernel[abs(y)] * geometryWeight
                * exp(-abs(luminance - centerLuminance) / sigma);
            // Centered sums preserve a constant signal exactly, including through temporal feedback.
            directSum += float4((sampleDirect.rgb - direct.rgb) * weight.x, sampleDirect.a * weight.x * weight.x);
            indirectSum += float4((sampleIndirect.rgb - indirect.rgb) * weight.y, sampleIndirect.a * weight.y * weight.y);
            weightSum += weight;
        }
    direct = float4(direct.rgb + directSum.rgb / weightSum.x, directSum.a / (weightSum.x * weightSum.x));
    indirect = float4(indirect.rgb + indirectSum.rgb / weightSum.y, indirectSum.a / (weightSum.y * weightSum.y));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint width, height;
    inputDirect.GetDimensions(width, height);
    uint2 pixel = thread.xy;
    if (pixel.x >= width || pixel.y >= height)
        return;
    float4 direct, indirect;
    filter(int2(pixel), uint2(width, height), direct, indirect);
#if SVGF_STAGE == 2
    float3 albedo = svgfAlbedoFactor(albedoImage.Load(int3(pixel, 0)).rgb);
    outputColor[pixel] = float4(emission.Load(int3(pixel, 0)).rgb + (direct.rgb + indirect.rgb) * albedo, 1.0);
#else
    outputDirect[pixel] = direct;
    outputIndirect[pixel] = indirect;
#if SVGF_STAGE == 0
    uint3 location = uint3(pixel, parameters.historyLayer);
    historyDirect[location] = direct;
    historyIndirect[location] = indirect;
#endif
#endif
}
