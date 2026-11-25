#include "scene/scene_data.slang"
#include "svgf_common.slang"

Texture2DArray<float4> currentColor;
Texture2D<float> currentDepth; // Unnormalized positive view-space depth.
Texture2D<float4> currentNormal;
Texture2D<float4> currentAlbedo;
[format("rgba32f")] RWTexture2DArray<float4> historyGeometry;
[format("rgba32f")] RWTexture2DArray<float4> historyLighting;
[format("rg32f")] RWTexture2DArray<float2> historyMoments;
[format("r32f")] RWTexture2DArray<float> historyLength;
[format("rgba32f")] RWTexture2D<float4> outputDirect;
[format("rgba32f")] RWTexture2D<float4> outputIndirect;

struct ReprojectionParameters
{
    float4x4 previousViewProjection;
    float4 previousDepthRow;
    uint writeLayer;
    uint historyValid;
    uint historyLength;
    float alpha;
};
ConstantBuffer<ReprojectionParameters> parameters;

static const float kNormalCosineThreshold = 0.95;
static const float kMinimumDepthTolerance = 0.001;
static const float kRelativeDepthTolerance = 0.01;

struct HistorySample
{
    float3 direct;
    float3 indirect;
    float2 directMoments; // E[L], E[L^2].
    float2 indirectMoments;
    float sampleCount;
};

bool gatherHistory(int2 pixel, uint2 extent, float projectedDepth, float tolerance, float3 normal,
    float weight, inout HistorySample history)
{
    if (!svgfContainsPixel(pixel, extent))
    {
        return false;
    }

    const uint frameLayer = parameters.writeLayer ^ 1u;
    const int3 location = int3(pixel, frameLayer);
    const float4 geometry = historyGeometry[location];
    const float previousDepth = geometry.x;
    const float3 previousNormal = geometry.yzw;
    if (previousDepth <= 0.0 || dot(normal, previousNormal) < kNormalCosineThreshold)
    {
        return false;
    }

    // Compare both depths in the previous camera's view space, not current-camera or NDC depth.
    if (abs(projectedDepth - previousDepth) > tolerance)
    {
        return false;
    }

    const int3 directLocation = int3(pixel, svgfLightingHistoryLayer(frameLayer, kDirectLightingLayer));
    const int3 indirectLocation = int3(pixel, svgfLightingHistoryLayer(frameLayer, kIndirectLightingLayer));
    history.direct += historyLighting[directLocation].rgb * weight;
    history.indirect += historyLighting[indirectLocation].rgb * weight;
    history.directMoments += historyMoments[directLocation] * weight;
    history.indirectMoments += historyMoments[indirectLocation] * weight;
    history.sampleCount += historyLength[location] * weight;
    return true;
}

bool reproject(float3 position, float3 normal, uint2 extent, out HistorySample history)
{
    // Current world position is also the previous position only for static geometry.
    history = {};
    if (parameters.historyValid == 0)
    {
        return false;
    }
    float4 clip = mul(parameters.previousViewProjection, float4(position, 1.0));
    if (clip.w <= 0.0)
    {
        return false;
    }
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
    {
        return false;
    }

    float projectedDepth = dot(parameters.previousDepthRow, float4(position, 1.0));
    if (projectedDepth <= 0.0)
    {
        return false;
    }
    float tolerance = max(kMinimumDepthTolerance, kRelativeDepthTolerance * projectedDepth);

    float2 previousPixel = clipToUv(ndc.xy) * float2(extent);
    int2 base = int2(floor(previousPixel - 0.5));
    float2 fraction = frac(previousPixel - 0.5);
    float weightSum = 0.0;
    for (int y = 0; y <= 1; ++y)
        for (int x = 0; x <= 1; ++x)
        {
            float weight = (x == 0 ? 1.0 - fraction.x : fraction.x)
                * (y == 0 ? 1.0 - fraction.y : fraction.y);
            if (weight == 0.0)
            {
                continue;
            }
            if (gatherHistory(base + int2(x, y), extent, projectedDepth, tolerance, normal, weight, history))
            {
                weightSum += weight;
            }
        }
    // A disoccluded bilinear footprint can still have a matching thin surface nearby.
    if (weightSum == 0.0)
    {
        int2 nearest = int2(floor(previousPixel));
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (gatherHistory(nearest + int2(x, y), extent, projectedDepth, tolerance, normal, 1.0, history))
                {
                    weightSum += 1.0;
                }
    }
    if (weightSum == 0.0)
    {
        return false;
    }
    history.direct /= weightSum;
    history.indirect /= weightSum;
    history.directMoments /= weightSum;
    history.indirectMoments /= weightSum;
    history.sampleCount /= weightSum;
    return true;
}

void accumulate(float3 sample, float3 previous, float2 previousMoments, float alpha,
    out float3 color, out float2 moments)
{
    float luminance = rec709Luminance(sample);
    color = lerp(previous, sample, alpha);
    moments = lerp(previousMoments, float2(luminance, luminance * luminance), alpha);
}

float2 spatialVariance(int2 pixel, uint2 extent, float depth, float3 normal, float2 centerLuminance)
{
    float2 gradient = svgfDepthGradient(currentDepth, pixel, depth, extent);
    float2 first = float2(0.0);
    float2 second = float2(0.0);
    float weightSum = 0.0;
    for (int y = -kSvgfSpatialVarianceRadius; y <= kSvgfSpatialVarianceRadius; ++y)
        for (int x = -kSvgfSpatialVarianceRadius; x <= kSvgfSpatialVarianceRadius; ++x)
        {
            int2 offset = int2(x, y);
            int2 neighbor = pixel + offset;
            if (!svgfContainsPixel(neighbor, extent))
            {
                continue;
            }
            float weight = svgfGeometryWeight(depth, currentDepth.Load(int3(neighbor, 0)), gradient, offset,
                normal, currentNormal.Load(int3(neighbor, 0)).xyz);
            weight *= exp(-float(x * x + y * y) / 18.0);
            float3 albedo = svgfAlbedoFactor(currentAlbedo.Load(int3(neighbor, 0)).rgb);
            float3 direct = currentColor.Load(int4(neighbor, kDirectLightingLayer, 0)).rgb / albedo;
            float3 indirect = currentColor.Load(int4(neighbor, kIndirectLightingLayer, 0)).rgb / albedo;
            float2 luminance = float2(rec709Luminance(direct), rec709Luminance(indirect));
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
    uint2 extent;
    outputDirect.GetDimensions(extent.x, extent.y);
    uint2 pixel = thread.xy;
    if (any(pixel >= extent))
    {
        return;
    }

    float depth = currentDepth.Load(int3(pixel, 0));
    bool valid = depth > 0.0;
    float2 ndc = uvToClip((float2(pixel) + 0.5) / float2(extent));
    float3 position = valid ? reconstructWorldPositionFromLinearDepth(ndc, depth, scene.camera) : float3(0.0);
    float3 normal = currentNormal.Load(int3(pixel, 0)).xyz;
    HistorySample previous = {};
    bool accepted = valid && reproject(position, normal, extent, previous);
    float count = accepted ? previous.sampleCount + 1.0 : 1.0;
    float alpha = accepted && count > parameters.historyLength ? parameters.alpha : 1.0 / count;

    float3 direct, indirect;
    float2 directMoments, indirectMoments;
    float3 albedo = svgfAlbedoFactor(currentAlbedo.Load(int3(pixel, 0)).rgb);
    float3 sampleDirect = currentColor.Load(int4(pixel, kDirectLightingLayer, 0)).rgb / albedo;
    float3 sampleIndirect = currentColor.Load(int4(pixel, kIndirectLightingLayer, 0)).rgb / albedo;
    accumulate(sampleDirect, previous.direct, previous.directMoments, alpha,
        direct, directMoments);
    accumulate(sampleIndirect, previous.indirect, previous.indirectMoments, alpha,
        indirect, indirectMoments);

    float2 variance = max(float2(0.0), float2(directMoments.y - directMoments.x * directMoments.x,
        indirectMoments.y - indirectMoments.x * indirectMoments.x));
    if (valid && count < kSvgfMinimumVarianceHistory)
    {
        float2 spatial = spatialVariance(int2(pixel), extent, depth, normal,
            float2(rec709Luminance(sampleDirect), rec709Luminance(sampleIndirect)));
        variance = spatial * kSvgfMinimumVarianceHistory / count;
    }

    uint3 location = uint3(pixel, parameters.writeLayer);
    // Keep the quantization of the RGBA16F normal history.
    float2 normalXY = unpackHalf2x16ToFloat(packHalf2x16(normal.xy));
    float normalZ = unpackHalf2x16ToFloat(packHalf2x16(float2(normal.z, 0.0))).x;
    historyGeometry[location] = float4(valid ? depth : 0.0, normalXY, normalZ);
    historyLength[location] = count;
    historyMoments[uint3(pixel, svgfLightingHistoryLayer(parameters.writeLayer, kDirectLightingLayer))] = directMoments;
    historyMoments[uint3(pixel, svgfLightingHistoryLayer(parameters.writeLayer, kIndirectLightingLayer))] = indirectMoments;
    outputDirect[pixel] = float4(direct, variance.x);
    outputIndirect[pixel] = float4(indirect, variance.y);
}
