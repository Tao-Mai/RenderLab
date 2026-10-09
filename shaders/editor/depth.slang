#include "view.slang"

struct VertexInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

struct PushConstants
{
    float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[shader("vertex")]
float4 vertMain(VertexInput input) : SV_Position
{
    precise float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    precise float4 clipPosition = mul(editorView.viewProjection, worldPosition);
    return clipPosition;
}

[shader("fragment")]
void fragMain() {}
