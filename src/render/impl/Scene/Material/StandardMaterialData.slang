#ifndef RENDERLAB_STANDARD_MATERIAL_DATA_SLANG
#define RENDERLAB_STANDARD_MATERIAL_DATA_SLANG

#include "material/texture_data.slang"

static const uint kAlphaModeOpaque = 0;
static const uint kAlphaModeMask = 1;
static const uint kShadingModelMetalRough = 0;
static const uint kShadingModelSpecGloss = 1;

struct StandardMaterialData
{
    float4 baseColor;
    float4 specularParams;
    float3 emissiveColor;
    float emissiveFactor;
    float3 transmissionColor;
    float diffuseTransmission;
    float specularTransmission;
    float indexOfRefraction;
    float normalScale;
    float alphaCutoff;
    uint shadingModel;
    uint alphaMode;
    uint doubleSided;
    uint thinSurface;
    uint normalEncoding;
    uint padding;
    TextureRef baseColorTexture;
    TextureRef specularTexture;
    TextureRef emissiveTexture;
    TextureRef normalTexture;
    TextureRef transmissionTexture;

    property float3 color { get { return baseColor.rgb; } }
    property float opacity { get { return baseColor.a; } }
    property float roughness { get { return specularParams.g; } }
    property float metallic { get { return specularParams.b; } }
    property float3 specularColor { get { return specularParams.rgb; } }
    property float glossiness { get { return specularParams.a; } }
};

StructuredBuffer<StandardMaterialData> materials;

float4 sampleMaterialTexture(TextureRef texture, float2 uv, float4 uniformValue)
{
    return texture.textureIndex == kInvalidMaterialTexture ? uniformValue : sampleTextureLevel(texture, uv);
}

float3 evalMaterialEmission(StandardMaterialData material, float2 uv)
{
    return sampleMaterialTexture(material.emissiveTexture, uv, float4(material.emissiveColor, 1.0)).rgb
        * material.emissiveFactor;
}

#endif
