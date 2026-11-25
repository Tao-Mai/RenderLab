#include "scene/scene_data.slang"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

TextureCube<float4> radianceMap;

SamplerState radianceSampler;

[shader("vertex")]
VertexOutput vertMain(uint vertexId : SV_VertexID)
{
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    const float2 ndc = corner * 2.0 - 1.0;
    return {float4(ndc, 1.0, 1.0), ndc};
}

[shader("fragment")]
[earlydepthstencil]
float4 fragMain(VertexOutput input) : SV_Target
{
    const float4 world = mul(scene.camera.inverseViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(
        world.xyz / world.w - scene.camera.cameraPosition.xyz);

    return float4(radianceMap.SampleLevel(radianceSampler, direction, 0.0).rgb, 1.0);
}
