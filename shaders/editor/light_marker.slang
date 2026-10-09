#include "view.slang"

struct VertexInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

struct PushConstants
{
    float4x4 model;
    float4 color;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    precise float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    precise float4 clipPosition = mul(editorView.viewProjection, worldPosition);
    output.position = clipPosition;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    return pushConstants.color;
}
