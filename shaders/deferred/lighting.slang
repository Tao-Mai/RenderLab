#include "../scene_lighting.slang"

// Pass set 3; bindings match RenderInterface's G-buffer bindings.
[[vk::binding(0, 3)]] Texture2D<float4> albedoOcclusion;
[[vk::binding(1, 3)]] Texture2D<float4> normalRoughness;
[[vk::binding(2, 3)]] Texture2D<float4> emissiveMetallic;
[[vk::binding(3, 3)]] Texture2D<float> sceneDepth;
[[vk::binding(4, 3)]] Texture2D<float4> sheenColorRoughness;

struct DeferredLightingParameters
{
    float4 viewport;
};
[[vk::push_constant]] ConstantBuffer<DeferredLightingParameters> parameters;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexId : SV_VertexID)
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return {float4(corner * 2.0 - 1.0, 0.0, 1.0)};
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    uint2 pixel = uint2(input.position.xy);
    float depth = sceneDepth.Load(int3(pixel, 0));
    if (depth >= 1.0)
        return float4(0.0, 0.0, 0.0, 1.0);

    float2 ndc = (input.position.xy - parameters.viewport.xy) / parameters.viewport.zw * 2.0 - 1.0;
    float4 worldPosition = mul(sceneUniforms.inverseViewProjection, float4(ndc, depth, 1.0));
    float4 albedo = albedoOcclusion.Load(int3(pixel, 0));
    float4 normal = normalRoughness.Load(int3(pixel, 0));
    float4 emissive = emissiveMetallic.Load(int3(pixel, 0));

    Surface surface;
    surface.worldPosition = worldPosition.xyz / worldPosition.w;
    surface.normal = normalize(normal.xyz);
    surface.baseColor = float4(albedo.rgb, 1.0);
    surface.occlusion = albedo.a;
    surface.roughness = normal.a;
    surface.metallic = emissive.a;
    surface.emissive = emissive.rgb;
    float4 sheen = sheenColorRoughness.Load(int3(pixel, 0));
    surface.sheenColor = sheen.rgb;
    surface.sheenRoughness = sheen.a;
    return float4(evaluateLighting(surface, pixel), 1.0);
}
