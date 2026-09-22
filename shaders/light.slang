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

struct Light
{
    float3 color;
    float intensity;
    float3 position;
    float range;
    float3 direction;
    float padding0;
    float2 areaSize;
    float cosInner;
    float cosOuter;
    uint type;
    uint enabled;
    uint castShadow;
    uint padding1;
};

struct SceneUniforms
{
    float4x4 viewProjection;
    Light light;
    float3 cameraPosition;
    float padding2;
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
float4 fragMain(VertexOutput input) : SV_Target
{
    return pushConstants.color;
}
