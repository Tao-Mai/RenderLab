#ifndef RENDERLAB_STANDARD_MATERIAL_DATA_SLANG
#define RENDERLAB_STANDARD_MATERIAL_DATA_SLANG

static const uint kMaxMaterialTextures    = 1024;
static const uint kMaxMaterialSamplers    = 32;
static const uint kInvalidMaterialTexture = 0xffffffff;
static const uint kAlphaModeOpaque        = 0;
static const uint kAlphaModeMask          = 1;
static const uint kShadingModelMetalRough = 0;
static const uint kShadingModelSpecGloss  = 1;

struct TextureRef
{
    uint textureIndex;
    uint samplerIndex;
};

struct StandardMaterialData
{
    float4     baseColor;
    float4     specularParams;
    float3     emissiveColor;
    float      emissiveFactor;
    float3     transmissionColor;
    float      diffuseTransmission;
    float      specularTransmission;
    float      indexOfRefraction;
    float      normalScale;
    float      alphaCutoff;
    uint       shadingModel;
    uint       alphaMode;
    uint       doubleSided;
    uint       thinSurface;
    uint       normalEncoding;
    uint       padding;
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

#endif
