#include "lighting_data.slang"

#ifndef SHADOW_TYPE
#error SHADOW_TYPE must be specified when compiling the shadow shader
#endif

// Match LightType: Point = 0, Directional = 1, RectArea = 2, Spot = 3.
#define SHADOW_TYPE_POINT 0
#define SHADOW_TYPE_DIRECTIONAL 1
#define SHADOW_TYPE_RECT_AREA 2
#define SHADOW_TYPE_SPOT 3

struct VertexInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

struct PushConstants
{
    float4x4 model;
    uint lightIdx;
    uint cascadeIndex;
    uint padding1;
    uint padding2;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexOutput
{
    float4 position : SV_Position;
#if SHADOW_TYPE == SHADOW_TYPE_POINT
    float3 worldPosition : POSITION0;
#endif
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input
#if SHADOW_TYPE == SHADOW_TYPE_POINT
    , uint viewID : SV_ViewID
#endif
)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    const LightData light = lights[pushConstants.lightIdx];
#if SHADOW_TYPE == SHADOW_TYPE_POINT
    output.position = mul(pointShadowData[light.shadowIdx].vp[viewID], worldPosition);
    output.worldPosition = worldPosition.xyz;
#elif SHADOW_TYPE == SHADOW_TYPE_DIRECTIONAL
    output.position = mul(directionalShadowData[light.shadowIdx].cascades[pushConstants.cascadeIndex].vp, worldPosition);
#elif SHADOW_TYPE == SHADOW_TYPE_RECT_AREA
    output.position = mul(rectAreaShadowData[light.shadowIdx].vp, worldPosition);
#elif SHADOW_TYPE == SHADOW_TYPE_SPOT
    output.position = mul(spotShadowData[light.shadowIdx].vp, worldPosition);
#else
#error Unsupported SHADOW_TYPE
#endif
    return output;
}

[shader("fragment")]
float fragMain(VertexOutput input) : SV_Depth
{
#if SHADOW_TYPE == SHADOW_TYPE_POINT
    // Depth 0..1 stores radial distance divided by the point light range.
    const LightData light = lights[pushConstants.lightIdx];
    return saturate(distance(input.worldPosition,
        light.positionRange.xyz) / light.positionRange.w);
#else
    // Rasterized depth uses the same VP as scene shadow lookup.
    return input.position.z;
#endif
}
