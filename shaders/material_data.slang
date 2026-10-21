#ifndef RENDERLAB_MATERIAL_DATA_SLANG
#define RENDERLAB_MATERIAL_DATA_SLANG

static const uint kAlphaModeOpaque = 0;
static const uint kAlphaModeMask = 1;
static const uint kAlphaModeBlend = 2;
static const uint kMaxMaterialTextures = 128;
static const uint kMaxMaterialSamplers = 32;

struct TextureRef
{
    uint textureIndex;
    uint samplerIndex;
};

// Fixed SSBO stride shared by every rendering route and material variant.
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

[[vk::binding(0, 1)]] StructuredBuffer<MaterialData> materials;
[[vk::binding(1, 1)]] Texture2D<float4> textures[kMaxMaterialTextures];
[[vk::binding(2, 1)]] SamplerState samplers[kMaxMaterialSamplers];

float4 sampleTexture(TextureRef texture, float2 uv)
{
    uint textureIndex = NonUniformResourceIndex(texture.textureIndex);
    uint samplerIndex = NonUniformResourceIndex(texture.samplerIndex);
    return textures[textureIndex].Sample(samplers[samplerIndex], uv);
}

float4 sampleTextureLevel(TextureRef texture, float2 uv, float level = 0.0)
{
    uint textureIndex = NonUniformResourceIndex(texture.textureIndex);
    uint samplerIndex = NonUniformResourceIndex(texture.samplerIndex);
    return textures[textureIndex].SampleLevel(samplers[samplerIndex], uv, level);
}

void applyAlphaTest(MaterialData material, float alpha)
{
    if (material.alphaMode == kAlphaModeMask && alpha < material.alphaCutoff)
        discard;
}

#endif
