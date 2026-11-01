#ifndef RENDERLAB_TEXTURE_DATA_SLANG
#define RENDERLAB_TEXTURE_DATA_SLANG

static const uint kMaxMaterialTextures = 128;
static const uint kMaxMaterialSamplers = 32;
struct TextureRef { uint textureIndex; uint samplerIndex; };

[[vk::binding(1, 2)]] Texture2D<float4> textures[kMaxMaterialTextures];
[[vk::binding(2, 2)]] SamplerState samplers[kMaxMaterialSamplers];

float4 sampleTexture(TextureRef texture, float2 uv)
{
    return textures[NonUniformResourceIndex(texture.textureIndex)].Sample(
        samplers[NonUniformResourceIndex(texture.samplerIndex)], uv);
}

float4 sampleTextureLevel(TextureRef texture, float2 uv, float level = 0.0)
{
    return textures[NonUniformResourceIndex(texture.textureIndex)].SampleLevel(
        samplers[NonUniformResourceIndex(texture.samplerIndex)], uv, level);
}
#endif
