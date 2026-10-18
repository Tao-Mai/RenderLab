#include "../view_data.slang"

[[vk::binding(0, 0)]] Texture2D<float4> currentColor;
[[vk::binding(1, 0)]] Texture2D<float> currentDepth; // Unnormalized positive view-space depth.
[[vk::binding(2, 0)]] Texture2D<float4> currentNormal;
[[vk::binding(3, 0)]] [[vk::image_format("rgba32f")]] RWTexture2DArray<float4> historyColor;
[[vk::binding(4, 0)]] [[vk::image_format("r32f")]] RWTexture2DArray<float> historyDepth;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2DArray<float4> historyNormal;
[[vk::binding(6, 0)]] [[vk::image_format("rg32f")]] RWTexture2DArray<float2> historyMoments;
[[vk::binding(7, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputColor;
[[vk::binding(8, 0)]] ConstantBuffer<ViewUniforms> viewUniforms;

struct ReprojectionParameters
{
    float4x4 previousViewProjection;
    float4 previousViewport;
    float4 previousDepthRow;
    float4 viewport;
    uint writeLayer;
    uint historyValid;
    uint emaStartSampleCount;
    float emaAlpha;
};
[[vk::push_constant]] ConstantBuffer<ReprojectionParameters> parameters;

static const float kNormalCosineThreshold = 0.95;
static const float kMinimumDepthTolerance = 0.001;
static const float kRelativeDepthTolerance = 0.01;

bool reproject(float3 position, float3 normal, out float4 history, out float2 moments)
{
    // Current world position is also the previous position only for static geometry.
    history = float4(0.0);
    moments = float2(0.0);
    if (parameters.historyValid == 0)
        return false;
    float4 clip = mul(parameters.previousViewProjection, float4(position, 1.0));
    if (clip.w <= 0.0)
        return false;
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
        return false;

    float2 previousPixel = parameters.previousViewport.xy
        + (ndc.xy * 0.5 + 0.5) * parameters.previousViewport.zw;
    int2 pixel = int2(floor(previousPixel));
    if (any(float2(pixel) + 0.5 < parameters.previousViewport.xy)
        || any(float2(pixel) + 0.5 >= parameters.previousViewport.xy + parameters.previousViewport.zw))
        return false;

    // Integer loads are nearest texel accesses; never interpolate surface attributes or history.
    int3 location = int3(pixel, parameters.writeLayer ^ 1u);
    float previousDepth = historyDepth[location];
    float3 previousNormal = historyNormal[location].xyz;
    if (previousDepth <= 0.0 || dot(normal, previousNormal) < kNormalCosineThreshold)
        return false;

    // Compare both depths in the previous camera's view space, not current-camera or NDC depth.
    float projectedDepth = dot(parameters.previousDepthRow, float4(position, 1.0));
    float tolerance = max(kMinimumDepthTolerance, kRelativeDepthTolerance * projectedDepth);
    if (projectedDepth <= 0.0 || abs(projectedDepth - previousDepth) > tolerance)
        return false;

    history = historyColor[location];
    moments = historyMoments[location];
    return history.a > 0.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint width, height;
    outputColor.GetDimensions(width, height);
    uint2 pixel = thread.xy;
    if (pixel.x >= width || pixel.y >= height)
        return;

    float3 sample = currentColor.Load(int3(pixel, 0)).rgb;
    // Linear Rec. 709 luminance, before exposure and tone mapping.
    float luminance = dot(sample, float3(0.2126, 0.7152, 0.0722));
    float2 sampleMoments = float2(luminance, luminance * luminance);
    float depth = currentDepth.Load(int3(pixel, 0));
    bool valid = depth > 0.0;
    float2 ndc = (float2(pixel) + 0.5 - parameters.viewport.xy) / parameters.viewport.zw * 2.0 - 1.0;
    float3 position = valid ? reconstructWorldPositionFromLinearDepth(ndc, depth, viewUniforms) : float3(0.0);
    float3 normal = currentNormal.Load(int3(pixel, 0)).xyz;
    float3 mean = sample;
    float2 moments = sampleMoments;
    float count = 1.0;
    float4 previous;
    float2 previousMoments;
    if (valid && reproject(position, normal, previous, previousMoments))
    {
        count = previous.a + 1.0;
        float alpha = count > parameters.emaStartSampleCount ? parameters.emaAlpha : 1.0 / count;
        mean = lerp(previous.rgb, sample, alpha);
        moments = lerp(previousMoments, sampleMoments, alpha);
    }

    uint3 location = uint3(pixel, parameters.writeLayer);
    historyColor[location] = float4(mean, count);
    historyDepth[location] = valid ? depth : 0.0;
    historyNormal[location] = float4(normal, 0.0);
    historyMoments[location] = moments;
    outputColor[pixel] = float4(mean, 1.0);
}
