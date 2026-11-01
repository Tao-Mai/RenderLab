#ifndef RENDERLAB_MATERIAL_DATA_SLANG
#define RENDERLAB_MATERIAL_DATA_SLANG

#include "texture_data.slang"

static const uint kAlphaModeOpaque = 0;
static const uint kAlphaModeMask = 1;
static const uint kAlphaModeBlend = 2;



// glTF material SSBO used by rasterization and realtime ray tracing.
struct MaterialData
{
    float4 baseColorFactor;
    float roughness;
    float metallic;
    float alphaCutoff;
    uint alphaMode;
    float normalScale;
    float occlusionStrength;
    uint doubleSided;
    uint padding;
    float4 emissiveFactor;
    float3 sheenColorFactor;
    float sheenRoughnessFactor;
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    float clearcoatNormalScale;
    float clearcoatPadding;
    TextureRef baseColorTexture;
    TextureRef normalTexture;
    TextureRef metallicTexture;
    TextureRef roughnessTexture;
    TextureRef emissiveTexture;
    TextureRef occlusionTexture;
    TextureRef sheenColorTexture;
    TextureRef sheenRoughnessTexture;
    TextureRef clearcoatTexture;
    TextureRef clearcoatRoughnessTexture;
    TextureRef clearcoatNormalTexture;
    uint2 texturePadding;
};

[[vk::binding(0, 2)]] StructuredBuffer<MaterialData> materials;
void applyAlphaTest(MaterialData material, float alpha)
{
    if (material.alphaMode == kAlphaModeMask && alpha < material.alphaCutoff)
        discard;
}

#endif
