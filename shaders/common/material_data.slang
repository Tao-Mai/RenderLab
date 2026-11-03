#ifndef RENDERLAB_MATERIAL_DATA_SLANG
#define RENDERLAB_MATERIAL_DATA_SLANG

#include "common/texture_data.slang"

static const uint kAlphaModeOpaque = 0;
static const uint kAlphaModeMask = 1;
static const uint kAlphaModeBlend = 2;
static const uint kMaterialWorkflowMetallicRoughness = 0;
static const uint kMaterialWorkflowSpecularGlossiness = 1;

// glTF material SSBO used by rasterization and realtime ray tracing.
struct MaterialData
{
    float4 colorFactor;
    property float3 color { get { return colorFactor.rgb; } }
    property float opacityFactor { get { return colorFactor.a; } }
    float roughness;
    float metallic;
    float alphaCutoff;
    uint alphaMode;
    float normalScale;
    float occlusionStrength;
    uint doubleSided;
    uint workflow;
    float4 emissiveFactor;
    property float3 emission { get { return emissiveFactor.rgb; } }
    float3 sheenColorFactor;
    float sheenRoughnessFactor;
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    float clearcoatNormalScale;
    float clearcoatPadding;
    float3 specularColorFactor;
    float specularFactor;
    float dielectricF0;
    float glossinessFactor;
    uint2 scalarPadding;
    TextureRef colorTexture;
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
    TextureRef specularColorTexture;
    TextureRef specularTexture;
    uint2 texturePadding;
};

[[vk::binding(0, 2)]] StructuredBuffer<MaterialData> materials;
void applyAlphaTest(MaterialData material, float alpha)
{
    if (material.alphaMode == kAlphaModeMask && alpha < material.alphaCutoff)
    {
        discard;
    }
}

#endif
