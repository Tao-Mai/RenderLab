#ifndef RENDERLAB_TEXTURE_DATA_SLANG
#define RENDERLAB_TEXTURE_DATA_SLANG

static const uint kMaxMaterialTextures = 1024;
static const uint kMaxMaterialSamplers = 32;
struct TextureRef { uint textureIndex; uint samplerIndex; };

struct MaterialTextures
{
    Texture2D<float4> textures[kMaxMaterialTextures];
    SamplerState samplers[kMaxMaterialSamplers];
};
ParameterBlock<MaterialTextures> materialTextures;

float4 sampleTexture(TextureRef texture, float2 uv)
{
    return materialTextures.textures[NonUniformResourceIndex(texture.textureIndex)].Sample(
        materialTextures.samplers[NonUniformResourceIndex(texture.samplerIndex)], uv);
}

float4 sampleTextureLevel(TextureRef texture, float2 uv, float level = 0.0)
{
    return materialTextures.textures[NonUniformResourceIndex(texture.textureIndex)].SampleLevel(
        materialTextures.samplers[NonUniformResourceIndex(texture.samplerIndex)], uv, level);
}
#endif
