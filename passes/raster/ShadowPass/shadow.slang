#include "scene/scene_data.slang"
#include "raster/shadow_data.slang"

#ifndef SHADOW_TYPE
#error SHADOW_TYPE must be specified when compiling the shadow shader
#endif

// Match LightType: Point = 0, Directional = 1, RectArea = 2, Spot = 3.
#define SHADOW_TYPE_POINT 0
#define SHADOW_TYPE_DIRECTIONAL 1
#define SHADOW_TYPE_RECT_AREA 2
#define SHADOW_TYPE_SPOT 3

#if SHADOW_TYPE == SHADOW_TYPE_POINT
StructuredBuffer<PointShadowData> shadowData;
#elif SHADOW_TYPE == SHADOW_TYPE_DIRECTIONAL
StructuredBuffer<DirectionalShadowData> shadowData;
#elif SHADOW_TYPE == SHADOW_TYPE_RECT_AREA
StructuredBuffer<RectAreaShadowData> shadowData;
#elif SHADOW_TYPE == SHADOW_TYPE_SPOT
StructuredBuffer<SpotShadowData> shadowData;
#else
#error Unsupported SHADOW_TYPE
#endif

struct VertexInput
{
    float3 position : POSITION;
};

struct PushConstants
{
    float4x4 model;
    uint lightIdx;
    uint cascadeIndex;
    uint faceIndex;
    uint padding2;
};

ConstantBuffer<PushConstants> pushConstants;

struct VertexOutput
{
    float4 position : SV_Position;
#if SHADOW_TYPE == SHADOW_TYPE_POINT
    float3 worldPosition : POSITION0;
#endif
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    const LightData light = scene.analyticLights[pushConstants.lightIdx];
#if SHADOW_TYPE == SHADOW_TYPE_POINT
    output.position = mul(shadowData[light.shadowIdx].vp[pushConstants.faceIndex], worldPosition);
    output.worldPosition = worldPosition.xyz;
#elif SHADOW_TYPE == SHADOW_TYPE_DIRECTIONAL
    output.position = mul(shadowData[light.shadowIdx].cascades[pushConstants.cascadeIndex].vp, worldPosition);
#else
    output.position = mul(shadowData[light.shadowIdx].vp, worldPosition);
#endif
    return output;
}

[shader("fragment")]
float fragMain(VertexOutput input) : SV_Depth
{
#if SHADOW_TYPE == SHADOW_TYPE_POINT
    // Depth 0..1 stores radial distance divided by the point light range.
    const LightData light = scene.analyticLights[pushConstants.lightIdx];
    return saturate(distance(input.worldPosition, light.position) / light.range);
#else
    // Rasterized depth uses the same VP as scene shadow lookup.
    return input.position.z;
#endif
}
