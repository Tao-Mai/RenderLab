#include "raster/scene_lighting.slang"
#include "raster/gbuffer_data.slang"

// Pass set 2; bindings match RenderInterface's G-buffer bindings.
[[vk::binding(0, 2)]] Texture2D<float4> diffuseOcclusion;
[[vk::binding(1, 2)]] Texture2D<float4> normalRoughness;
[[vk::binding(2, 2)]] Texture2D<float4> emissiveSpecularF90;
[[vk::binding(3, 2)]] Texture2D<float4> specularF0DiffuseFresnel;
[[vk::binding(4, 2)]] Texture2D<float4> sheenColorRoughness;
[[vk::binding(5, 2)]] Texture2D<float> sceneDepth;

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
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float2 ndc = (input.position.xy - parameters.viewport.xy) / parameters.viewport.zw * 2.0 - 1.0;
    float4 worldPosition = mul(sceneUniforms.inverseViewProjection, float4(ndc, depth, 1.0));
    GBufferData data = {
        diffuseOcclusion.Load(int3(pixel, 0)),
        normalRoughness.Load(int3(pixel, 0)),
        emissiveSpecularF90.Load(int3(pixel, 0)),
        specularF0DiffuseFresnel.Load(int3(pixel, 0)),
        sheenColorRoughness.Load(int3(pixel, 0)),
    };

    Surface surface;
    surface.worldPosition = worldPosition.xyz / worldPosition.w;
    surface.normal = normalize(data.normal);
    surface.brdf = data.brdf;
    surface.opacity = 1.0;
    surface.occlusion = data.occlusion;
    surface.emissive = data.emissive;
    surface.sheenColor = data.sheenColor;
    surface.sheenRoughness = data.sheenRoughness;
    return float4(evaluateLighting(surface, pixel), 1.0);
}
