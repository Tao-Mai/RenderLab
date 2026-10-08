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
};

[[vk::binding(0, 1)]]
Texture2D<float4> baseColorTexture;

[[vk::binding(1, 1)]]
SamplerState baseColorSampler;

[[vk::binding(2, 1)]]
ConstantBuffer<MaterialData> material;

void applyAlphaTest(float alpha)
{
    if (material.alphaMode == 1 && alpha < material.alphaCutoff)
        discard;
}

#endif
