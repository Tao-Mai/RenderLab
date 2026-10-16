#include "../mesh_surface.slang"

// One material layout for every opaque/masked surface. Clearcoat is intentionally omitted.
struct GBufferOutput
{
    float4 albedoOcclusion : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 emissiveMetallic : SV_Target2;
    float4 sheenColorRoughness : SV_Target3;
};

[shader("fragment")]
GBufferOutput fragMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace)
{
    Surface surface = sampleSurface(input, isFrontFace);
    return {
        float4(surface.baseColor.rgb, surface.occlusion),
        float4(surface.normal, surface.roughness),
        float4(surface.emissive, surface.metallic),
        float4(surface.sheenColor, surface.sheenRoughness),
    };
}
