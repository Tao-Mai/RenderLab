#ifndef RENDERLAB_MATERIAL_DATA_SLANG
#define RENDERLAB_MATERIAL_DATA_SLANG

struct MaterialData
{
    float4 baseColorFactor;
    float roughness;
    float metallic;
    float alphaCutoff;
    uint alphaMode;
    float normalScale;
    float occlusionStrength;
    float padding1;
    float padding2;
    float4 emissiveFactor;
    float3 sheenColorFactor;
    float sheenRoughnessFactor;
#if ENABLE_CLEARCOAT
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    float clearcoatNormalScale;
    float clearcoatPadding;
#endif
};

[[vk::binding(0, 1)]]
Texture2D<float4> baseColorTexture;

[[vk::binding(1, 1)]]
SamplerState baseColorSampler;

[[vk::binding(2, 1)]]
ConstantBuffer<MaterialData> material;

[[vk::binding(3, 1)]]
Texture2D<float4> normalTexture;

[[vk::binding(4, 1)]]
SamplerState normalSampler;

[[vk::binding(5, 1)]]
Texture2D<float4> metallicTexture;

[[vk::binding(6, 1)]]
SamplerState metallicSampler;

[[vk::binding(7, 1)]]
Texture2D<float4> roughnessTexture;

[[vk::binding(8, 1)]]
SamplerState roughnessSampler;

[[vk::binding(9, 1)]]
Texture2D<float4> emissiveTexture;

[[vk::binding(10, 1)]]
SamplerState emissiveSampler;

[[vk::binding(11, 1)]]
Texture2D<float4> occlusionTexture;

[[vk::binding(12, 1)]]
SamplerState occlusionSampler;

#if ENABLE_CLEARCOAT
[[vk::binding(13, 1)]]
Texture2D<float4> clearcoatTexture;

[[vk::binding(14, 1)]]
SamplerState clearcoatSampler;

[[vk::binding(15, 1)]]
Texture2D<float4> clearcoatRoughnessTexture;

[[vk::binding(16, 1)]]
SamplerState clearcoatRoughnessSampler;

[[vk::binding(17, 1)]]
Texture2D<float4> clearcoatNormalTexture;

[[vk::binding(18, 1)]]
SamplerState clearcoatNormalSampler;
#endif

#if ENABLE_SHEEN
[[vk::binding(19, 1)]] Texture2D<float4> sheenColorTexture;
[[vk::binding(20, 1)]] SamplerState sheenColorSampler;
[[vk::binding(21, 1)]] Texture2D<float4> sheenRoughnessTexture;
[[vk::binding(22, 1)]] SamplerState sheenRoughnessSampler;
#endif

void applyAlphaTest(float alpha)
{
    if (material.alphaMode == 1 && alpha < material.alphaCutoff)
        discard;
}

#endif
