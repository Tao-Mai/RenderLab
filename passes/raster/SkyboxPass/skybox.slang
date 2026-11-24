#include "scene/scene_data.slang"

struct VertexOutput
{
    float4 position : SV_Position;
};

struct SkyboxPushConstants
{
    float4 viewport;

    property float2 viewportOffset { get { return viewport.xy; } }
    property float2 viewportExtent { get { return viewport.zw; } }
};

ConstantBuffer<SkyboxPushConstants> pushConstants;

TextureCube<float4> radianceMap;

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
    const float2 ndc = uvToClip((input.position.xy - pushConstants.viewportOffset) / pushConstants.viewportExtent);
    const float4 world = mul(scene.globals[0].camera.inverseViewProjection,
        float4(ndc, 1.0, 1.0));
    const float3 direction = normalize(
        world.xyz / world.w - scene.globals[0].camera.cameraPosition.xyz);

    return float4(radianceMap.SampleLevel(radianceSampler, direction, 0.0).rgb, 1.0);
}
