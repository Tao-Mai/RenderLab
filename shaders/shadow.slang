#include "scene_data.slang"

#ifndef SHADOW_POINT
#error SHADOW_POINT must be specified when compiling the shadow shader
#endif

struct VertexInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

struct PushConstants
{
    float4x4 model;
    uint lightIdx;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexOutput
{
    float4 position : SV_Position;
#if SHADOW_POINT
    float3 worldPosition : POSITION0;
#endif
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input
#if SHADOW_POINT
    , uint viewID : SV_ViewID
#endif
)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    const LightData light = lights[pushConstants.lightIdx];
#if SHADOW_POINT
    output.position = mul(pointShadowData[light.shadowIdx].vp[viewID], worldPosition);
    output.worldPosition = worldPosition.xyz;
#else
    output.position = mul(rectAreaShadowData[light.shadowIdx].vp, worldPosition);
#endif
    return output;
}

[shader("fragment")]
float fragMain(VertexOutput input) : SV_Depth
{
#if SHADOW_POINT
    // Depth 0..1 stores radial distance divided by the point light range.
    const LightData light = lights[pushConstants.lightIdx];
    return saturate(distance(input.worldPosition,
        light.positionRange.xyz) / light.positionRange.w);
#else
    // Rasterized depth uses the same perspective VP as scene shadow lookup.
    return input.position.z;
#endif
}
