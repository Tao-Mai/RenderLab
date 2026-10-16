#ifndef RENDERLAB_RAYTRACING_MATERIAL_DATA_SLANG
#define RENDERLAB_RAYTRACING_MATERIAL_DATA_SLANG

static const uint kMaxRayTracingTextures = 128;
static const uint kMaxRayTracingSamplers = 32;

struct TextureRef
{
    uint textureIndex;
    uint samplerIndex;
};

struct MaterialData
{
    float4 baseColorFactor;
    float4 emissiveFactor;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint alphaMode;
    uint doubleSided;
    uint padding;
    TextureRef baseColorTexture;
    TextureRef normalTexture;
    TextureRef metallicTexture;
    TextureRef roughnessTexture;
    TextureRef emissiveTexture;
    TextureRef occlusionTexture;
};

[[vk::binding(3, 0)]] Texture2D<float4> textures[kMaxRayTracingTextures];
[[vk::binding(4, 0)]] SamplerState samplers[kMaxRayTracingSamplers];
[[vk::binding(6, 0)]] StructuredBuffer<MaterialData> materials;

float4 sampleTexture(TextureRef texture, float2 uv)
{
    uint textureIndex = NonUniformResourceIndex(texture.textureIndex);
    uint samplerIndex = NonUniformResourceIndex(texture.samplerIndex);
    return textures[textureIndex].SampleLevel(samplers[samplerIndex], uv, 0);
}

#endif
