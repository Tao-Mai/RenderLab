#include "../scene_data.slang"
#include "svgf_common.slang"

[[vk::binding(0, 1)]] Texture2D<float4> currentDirect;
[[vk::binding(1, 1)]] Texture2D<float4> currentIndirect;
[[vk::binding(2, 1)]] Texture2D<float> currentDepth; // Unnormalized positive view-space depth.
[[vk::binding(3, 1)]] Texture2D<float4> currentNormal;
[[vk::binding(4, 1)]] Texture2D<float4> currentAlbedo;
[[vk::binding(5, 1)]] [[vk::image_format("rgba32f")]] RWTexture2DArray<float4> historyDirect;
[[vk::binding(6, 1)]] [[vk::image_format("rgba32f")]] RWTexture2DArray<float4> historyIndirect;
[[vk::binding(7, 1)]] [[vk::image_format("r32f")]] RWTexture2DArray<float> historyDepth;
[[vk::binding(8, 1)]] [[vk::image_format("rgba16f")]] RWTexture2DArray<float4> historyNormal;
[[vk::binding(9, 1)]] [[vk::image_format("rgba32f")]] RWTexture2DArray<float4> historyDirectMoments;
[[vk::binding(10, 1)]] [[vk::image_format("rgba32f")]] RWTexture2DArray<float4> historyIndirectMoments;
[[vk::binding(11, 1)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> outputDirect;
[[vk::binding(12, 1)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> outputIndirect;

struct ReprojectionParameters
{
    float4x4 previousViewProjection;
    float4 previousViewport;
    float4 previousDepthRow;
    float4 viewport;
    uint writeLayer;
    uint historyValid;
    uint historyLength;
    float emaAlpha;
};
[[vk::push_constant]] ConstantBuffer<ReprojectionParameters> parameters;

static const float kNormalCosineThreshold = 0.95;
static const float kMinimumDepthTolerance = 0.001;
static const float kRelativeDepthTolerance = 0.01;

struct HistorySample
{
    float3 direct;
    float3 indirect;
    float3 directMoments; // E[L], E[L^2], sample count.
    float3 indirectMoments;
};

bool gatherHistory(int2 pixel, float projectedDepth, float tolerance, float3 normal,
    float weight, inout HistorySample history)
{
    if (any(float2(pixel) + 0.5 < parameters.previousViewport.xy)
        || any(float2(pixel) + 0.5 >= parameters.previousViewport.xy + parameters.previousViewport.zw))
        return false;

    int3 location = int3(pixel, parameters.writeLayer ^ 1u);
    float previousDepth = historyDepth[location];
    float3 previousNormal = historyNormal[location].xyz;
    if (previousDepth <= 0.0 || dot(normal, previousNormal) < kNormalCosineThreshold)
        return false;

    // Compare both depths in the previous camera's view space, not current-camera or NDC depth.
    if (abs(projectedDepth - previousDepth) > tolerance)
        return false;

    history.direct += historyDirect[location].rgb * weight;
    history.indirect += historyIndirect[location].rgb * weight;
    history.directMoments += historyDirectMoments[location].xyz * weight;
    history.indirectMoments += historyIndirectMoments[location].xyz * weight;
    return true;
}

bool reproject(float3 position, float3 normal, out HistorySample history)
{
    // Current world position is also the previous position only for static geometry.
    history = {};
    if (parameters.historyValid == 0)
        return false;
    float4 clip = mul(parameters.previousViewProjection, float4(position, 1.0));
    if (clip.w <= 0.0)
        return false;
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
        return false;

    float projectedDepth = dot(parameters.previousDepthRow, float4(position, 1.0));
    if (projectedDepth <= 0.0)
        return false;
    float tolerance = max(kMinimumDepthTolerance, kRelativeDepthTolerance * projectedDepth);

    float2 previousPixel = parameters.previousViewport.xy
        + (ndc.xy * 0.5 + 0.5) * parameters.previousViewport.zw;
    int2 base = int2(floor(previousPixel - 0.5));
    float2 fraction = frac(previousPixel - 0.5);
    float weightSum = 0.0;
    for (int y = 0; y <= 1; ++y)
        for (int x = 0; x <= 1; ++x)
        {
            float weight = (x == 0 ? 1.0 - fraction.x : fraction.x)
                * (y == 0 ? 1.0 - fraction.y : fraction.y);
            if (weight == 0.0)
                continue;
            if (gatherHistory(base + int2(x, y), projectedDepth, tolerance, normal, weight, history))
                weightSum += weight;
        }
    // A disoccluded bilinear footprint can still have a matching thin surface nearby.
    if (weightSum == 0.0)
    {
        int2 nearest = int2(floor(previousPixel));
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (gatherHistory(nearest + int2(x, y), projectedDepth, tolerance, normal, 1.0, history))
                    weightSum += 1.0;
    }
    if (weightSum == 0.0)
        return false;
    history.direct /= weightSum;
    history.indirect /= weightSum;
    history.directMoments /= weightSum;
    history.indirectMoments /= weightSum;
    return true;
}

void accumulate(float3 sample, bool valid, float3 previous, float3 previousMoments,
    out float3 color, out float3 moments)
{
    // Use the same weight for RGB, luminance and squared luminance.
    float count = valid ? previousMoments.z + 1.0 : 1.0;
    float alpha = valid && count > parameters.historyLength ? parameters.emaAlpha : 1.0 / count;
    float luminance = svgfLuminance(sample);
    color = lerp(previous, sample, alpha);
    moments = float3(lerp(previousMoments.xy, float2(luminance, luminance * luminance), alpha), count);
}

float2 spatialVariance(int2 pixel, uint2 extent, float depth, float3 normal, float2 centerLuminance)
{
    float2 gradient = svgfDepthGradient(currentDepth, pixel, depth, extent, parameters.viewport);
    float2 first = float2(0.0);
    float2 second = float2(0.0);
    float weightSum = 0.0;
    for (int y = -kSvgfSpatialVarianceRadius; y <= kSvgfSpatialVarianceRadius; ++y)
        for (int x = -kSvgfSpatialVarianceRadius; x <= kSvgfSpatialVarianceRadius; ++x)
        {
            int2 offset = int2(x, y);
            int2 neighbor = pixel + offset;
            if (!svgfContainsPixel(neighbor, extent, parameters.viewport))
                continue;
            float weight = svgfGeometryWeight(depth, currentDepth.Load(int3(neighbor, 0)), gradient, offset,
                normal, currentNormal.Load(int3(neighbor, 0)).xyz);
            weight *= exp(-float(x * x + y * y) / 18.0);
            float3 albedo = svgfAlbedoFactor(currentAlbedo.Load(int3(neighbor, 0)).rgb);
            float2 luminance = float2(svgfLuminance(currentDirect.Load(int3(neighbor, 0)).rgb / albedo),
                svgfLuminance(currentIndirect.Load(int3(neighbor, 0)).rgb / albedo));
            // Centered moments avoid cancellation when a bright, nearly uniform surface has little noise.
            luminance -= centerLuminance;
            first += weight * luminance;
            second += weight * luminance * luminance;
            weightSum += weight;
        }
    first /= weightSum;
    return max(float2(0.0), second / weightSum - first * first);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint width, height;
    outputDirect.GetDimensions(width, height);
    uint2 pixel = thread.xy;
    if (pixel.x >= width || pixel.y >= height)
        return;

    float depth = currentDepth.Load(int3(pixel, 0));
    bool valid = depth > 0.0 && svgfContainsPixel(int2(pixel), uint2(width, height), parameters.viewport);
    float2 ndc = (float2(pixel) + 0.5 - parameters.viewport.xy) / parameters.viewport.zw * 2.0 - 1.0;
    float3 position = valid ? reconstructWorldPositionFromLinearDepth(ndc, depth, sceneUniforms) : float3(0.0);
    float3 normal = currentNormal.Load(int3(pixel, 0)).xyz;
    HistorySample previous = {};
    bool accepted = valid && reproject(position, normal, previous);

    float3 direct, indirect;
    float3 directMoments, indirectMoments;
    float3 albedo = svgfAlbedoFactor(currentAlbedo.Load(int3(pixel, 0)).rgb);
    float3 sampleDirect = currentDirect.Load(int3(pixel, 0)).rgb / albedo;
    float3 sampleIndirect = currentIndirect.Load(int3(pixel, 0)).rgb / albedo;
    accumulate(sampleDirect, accepted, previous.direct, previous.directMoments,
        direct, directMoments);
    accumulate(sampleIndirect, accepted, previous.indirect, previous.indirectMoments,
        indirect, indirectMoments);

    float2 variance = max(float2(0.0), float2(directMoments.y - directMoments.x * directMoments.x,
        indirectMoments.y - indirectMoments.x * indirectMoments.x));
    if (valid && min(directMoments.z, indirectMoments.z) < kSvgfMinimumVarianceHistory)
    {
        float2 spatial = spatialVariance(int2(pixel), uint2(width, height), depth, normal,
            float2(svgfLuminance(sampleDirect), svgfLuminance(sampleIndirect)));
        if (directMoments.z < kSvgfMinimumVarianceHistory)
            variance.x = spatial.x * kSvgfMinimumVarianceHistory / directMoments.z;
        if (indirectMoments.z < kSvgfMinimumVarianceHistory)
            variance.y = spatial.y * kSvgfMinimumVarianceHistory / indirectMoments.z;
    }

    uint3 location = uint3(pixel, parameters.writeLayer);
    historyDepth[location] = valid ? depth : 0.0;
    historyNormal[location] = float4(normal, 0.0);
    historyDirectMoments[location] = float4(directMoments, 0.0);
    historyIndirectMoments[location] = float4(indirectMoments, 0.0);
    outputDirect[pixel] = float4(direct, variance.x);
    outputIndirect[pixel] = float4(indirect, variance.y);
}
