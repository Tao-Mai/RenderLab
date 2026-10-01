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

// The first three fields match the offsets in ShaderData.h::SceneUniforms.
struct SceneUniforms
{
    float4x4 viewProjection;
    float4 lightColorIntensity;
    float4 lightPositionRange;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneUniforms> sceneUniforms;

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
    // Depth 0..1 stores radial distance divided by the point light range.
    return saturate(distance(input.worldPosition,
        sceneUniforms.lightPositionRange.xyz) / sceneUniforms.lightPositionRange.w);
}
