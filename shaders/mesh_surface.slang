#ifndef RENDERLAB_MESH_SURFACE_SLANG
#define RENDERLAB_MESH_SURFACE_SLANG

#include "mesh_vertex.slang"
#include "material_data.slang"
#include "surface.slang"
#include "normal_mapping.slang"

struct VertexOutput
{
    precise float4 position : SV_Position;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : POSITION0;
    float4 worldTangent : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition;
    output.position = transformMeshPosition(input.position, worldPosition);
    float3x3 model = (float3x3)pushConstants.model;
    // cofactor矩阵，伴随矩阵的转置
    float3x3 cofactor = float3x3(
        cross(model[1], model[2]),
        cross(model[2], model[0]),
        cross(model[0], model[1]));
    float handedness = determinant(model) < 0.0 ? -1.0 : 1.0;
    output.worldNormal = normalize(mul(cofactor, input.normal)) * handedness;
    output.worldTangent = float4(normalize(mul(model, input.tangent.xyz)), input.tangent.w * handedness);
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

Surface sampleSurface(VertexOutput input, bool isFrontFace)
{
    MaterialData material = materials[pushConstants.materialIndex];
    float3 geometricNormal = normalize(input.worldNormal);
    Surface surface;
    surface.worldPosition = input.worldPosition;
    surface.normal = applyNormalMap(geometricNormal, input.worldTangent,
        sampleTexture(material.normalTexture, input.uv).xyz, material.normalScale);
    surface.normal = isFrontFace ? surface.normal : -surface.normal;
    surface.baseColor = sampleTexture(material.baseColorTexture, input.uv) * material.baseColorFactor;
    applyAlphaTest(material, surface.baseColor.a);
    surface.roughness = saturate(material.roughness * sampleTexture(material.roughnessTexture, input.uv).g);
    surface.metallic = saturate(material.metallic * sampleTexture(material.metallicTexture, input.uv).b);
    surface.occlusion = lerp(1.0, sampleTexture(material.occlusionTexture, input.uv).r, material.occlusionStrength);
    surface.emissive = sampleTexture(material.emissiveTexture, input.uv).rgb * material.emissiveFactor.rgb;
#if ENABLE_SHEEN
    surface.sheenColor = sampleTexture(material.sheenColorTexture, input.uv).rgb * material.sheenColorFactor;
    surface.sheenRoughness = saturate(sampleTexture(material.sheenRoughnessTexture, input.uv).a
        * material.sheenRoughnessFactor);
#endif
#if ENABLE_CLEARCOAT
    surface.clearcoatNormal = applyNormalMap(geometricNormal, input.worldTangent,
        sampleTexture(material.clearcoatNormalTexture, input.uv).xyz, material.clearcoatNormalScale);
    surface.clearcoatNormal = isFrontFace ? surface.clearcoatNormal : -surface.clearcoatNormal;
    surface.clearcoatFactor = saturate(material.clearcoatFactor * sampleTexture(material.clearcoatTexture, input.uv).r);
    surface.clearcoatRoughness = saturate(material.clearcoatRoughnessFactor * sampleTexture(material.clearcoatRoughnessTexture, input.uv).g);
#endif
    return surface;
}

#endif
