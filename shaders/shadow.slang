#include "scene_data.slang"

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
    float3 worldPosition : POSITION0;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input, uint viewID : SV_ViewID)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    const LightData light = lights[pushConstants.lightIdx];
    output.position = mul(pointShadowData[light.shadowIdx].vp[viewID], worldPosition);
    output.worldPosition = worldPosition.xyz;
    return output;
}

[shader("fragment")]
float fragMain(VertexOutput input) : SV_Depth
{
    // Depth 0..1 stores radial distance divided by the point light range.
    const LightData light = lights[pushConstants.lightIdx];
    return saturate(distance(input.worldPosition,
        light.positionRange.xyz) / light.positionRange.w);
}
