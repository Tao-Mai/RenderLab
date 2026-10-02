#include "scene_data.slang"

struct VertexInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

struct PushConstants
{
    float4x4 model;
    float4x4 viewProjection;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION0;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    output.position = mul(pushConstants.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    return output;
}

[shader("fragment")]
float fragMain(VertexOutput input) : SV_Depth
{
    // ShadowPass draws only for the first light when it is an enabled shadow-casting point light.
    // Depth 0..1 stores radial distance divided by the point light range.
    const LightData light = lights[0];
    return saturate(distance(input.worldPosition,
        light.positionRange.xyz) / light.positionRange.w);
}
