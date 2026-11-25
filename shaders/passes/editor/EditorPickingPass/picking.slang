#include "scene/scene_data.slang"

struct VertexInput
{
    float3 position : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

struct PushConstants
{
    float4x4 model;
    uint selectionId;
    uint padding0;
    uint padding1;
    uint padding2;
};

ConstantBuffer<PushConstants> pushConstants;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    precise float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    precise float4 clipPosition = mul(scene.camera.viewProjection, worldPosition);
    output.position = clipPosition;
    return output;
}

[shader("fragment")]
uint fragMain(VertexOutput input) : SV_Target
{
    return pushConstants.selectionId;
}
