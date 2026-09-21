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
    uint selectionId;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct SceneUniforms
{
    float4x4 viewProjection;
    float4 lightColorIntensity;
    float4 lightPositionRange;
    float4 lightDirection;
    float4 lightAreaSizeCone;
    uint4 lightFlags;
    float3 cameraPosition;
    float padding0;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[[vk::binding(0, 0)]]
ConstantBuffer<SceneUniforms> sceneUniforms;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    output.position = mul(sceneUniforms.viewProjection, worldPosition);
    return output;
}

[shader("fragment")]
uint fragMain(VertexOutput input) : SV_Target
{
    return pushConstants.selectionId;
}
