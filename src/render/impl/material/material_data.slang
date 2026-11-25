#ifndef RENDERLAB_MATERIAL_DATA_SLANG
#define RENDERLAB_MATERIAL_DATA_SLANG

#include "render/impl/material/texture_data.slang"
#include "render/resources/SceneLightConstants.h"

static const uint kAlphaModeOpaque = 0;
static const uint kAlphaModeMask = 1;
static const uint kAlphaModeBlend = 2;
static const uint kMaterialWorkflowMetallicRoughness = 0;
static const uint kMaterialWorkflowSpecularGlossiness = 1;

struct MaterialData
{
    float4 colorFactor;
    float4 emissiveFactor;
    float3 specularColorFactor;
    float specularFactor;
    float roughness;
    float metallic;
    float alphaCutoff;
    uint alphaMode;
    float normalScale;
    uint emissiveFlags;
    uint workflow;
    float dielectricF0;
    float glossinessFactor;
    uint normalEncoding;
    TextureRef colorTexture;
    TextureRef normalTexture;
    TextureRef metallicTexture;
    TextureRef roughnessTexture;
    TextureRef emissiveTexture;
    TextureRef specularColorTexture;
    TextureRef specularTexture;

    property float3 color { get { return colorFactor.rgb; } }
    property float opacityFactor { get { return colorFactor.a; } }
    property float3 emission { get { return emissiveFactor.rgb; } }
    property emissionTwoSided : bool { get { return (emissiveFlags & kEmissiveTwoSided) != 0; } }
};

StructuredBuffer<MaterialData> materials;
#endif
