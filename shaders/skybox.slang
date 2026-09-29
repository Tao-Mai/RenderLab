struct VertexOutput
{
    float4 position : SV_Position;
};

struct SkyboxPushConstants
{
    float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 viewport;
};

[[vk::push_constant]]
ConstantBuffer<SkyboxPushConstants> pushConstants;

[[vk::binding(7, 0)]]
TextureCube<float4> radianceMap;

[[vk::binding(8, 0)]]
SamplerState radianceSampler;

[shader("vertex")]
VertexOutput vertMain(uint vertexId : SV_VertexID)
{
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);

    VertexOutput output;
    output.position = float4(corner * 2.0 - 1.0, 1.0, 1.0);
    return output;
}

[shader("fragment")]
[earlydepthstencil]
float4 fragMain(VertexOutput input) : SV_Target
{
    const float2 ndc =
        (input.position.xy - pushConstants.viewport.xy) /
        pushConstants.viewport.zw * 2.0 - 1.0;
    const float4 world = mul(pushConstants.inverseViewProjection,
        float4(ndc, 1.0, 1.0));
    const float3 direction = normalize(
        world.xyz / world.w - pushConstants.cameraPosition.xyz);

    return float4(radianceMap.SampleLevel(radianceSampler, direction, 0.0).rgb, 1.0);
}
