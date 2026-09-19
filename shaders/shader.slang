
struct VertexInput{
    [[vk::location(0)]]
    float3 position : POSITION;

    [[vk::location(1)]]
    float3 normal : NORMAL;

    [[vk::location(2)]]
    float2 uv : TEXCOORD;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

struct PushConstants
{
    float4x4 modelViewProjection;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(pushConstants.modelViewProjection, float4(input.position, 1.0));
    float3 normalColor = input.normal * 0.5 + 0.5;
    output.color = lerp(normalColor, float3(input.uv, 1.0), 0.15);
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
