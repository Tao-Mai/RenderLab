#ifndef RENDERLAB_SVGF_COMMON_SLANG
#define RENDERLAB_SVGF_COMMON_SLANG

// Schied et al., HPG 2017: fixed edge-stopping parameters shared by both lighting signals.
static const float kSvgfPhiDepth = 1.0;
static const float kSvgfPhiNormal = 128.0;
static const float kSvgfPhiLuminance = 4.0;
static const float kSvgfEpsilon = 0.0001;
static const float kSvgfMinimumDepthTolerance = 0.001;
static const uint kSvgfMinimumVarianceHistory = 4;
static const int kSvgfSpatialVarianceRadius = 3;
static const float kSvgfMinimumAlbedo = 0.01;

float3 svgfAlbedoFactor(float3 albedo)
{
    // Use the same factor for division and remodulation, including black channels with specular energy.
    return max(albedo, float3(kSvgfMinimumAlbedo));
}

float svgfLuminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

bool svgfContainsPixel(int2 pixel, uint2 extent, float4 viewport)
{
    float2 center = float2(pixel) + 0.5;
    return all(pixel >= 0) && all(pixel < int2(extent))
        && all(center >= viewport.xy) && all(center < viewport.xy + viewport.zw);
}

float svgfDepthDerivative(Texture2D<float> depths, int2 pixel, int2 axis, float depth,
    uint2 extent, float4 viewport)
{
    bool hasBefore = svgfContainsPixel(pixel - axis, extent, viewport);
    bool hasAfter = svgfContainsPixel(pixel + axis, extent, viewport);
    float before = hasBefore ? depths.Load(int3(pixel - axis, 0)) : -1.0;
    float after = hasAfter ? depths.Load(int3(pixel + axis, 0)) : -1.0;
    hasBefore = hasBefore && before > 0.0;
    hasAfter = hasAfter && after > 0.0;
    float backward = depth - before;
    float forward = after - depth;
    // Prefer the one-sided derivative with the smaller jump at a silhouette.
    if (hasBefore && hasAfter)
        return abs(backward) < abs(forward) ? backward : forward;
    if (hasBefore)
        return backward;
    return hasAfter ? forward : 0.0;
}

float2 svgfDepthGradient(Texture2D<float> depths, int2 pixel, float depth, uint2 extent, float4 viewport)
{
    return float2(svgfDepthDerivative(depths, pixel, int2(1, 0), depth, extent, viewport),
        svgfDepthDerivative(depths, pixel, int2(0, 1), depth, extent, viewport));
}

float svgfGeometryWeight(float depth, float sampleDepth, float2 gradient, int2 offset,
    float3 normal, float3 sampleNormal)
{
    if (sampleDepth <= 0.0)
        return 0.0;
    float normalWeight = pow(saturate(dot(normal, sampleNormal)), kSvgfPhiNormal);
    float depthTolerance = kSvgfPhiDepth * abs(dot(gradient, float2(offset)))
        + max(kSvgfMinimumDepthTolerance, abs(depth) * kSvgfEpsilon);
    return normalWeight * exp(-abs(depth - sampleDepth) / depthTolerance);
}

#endif
