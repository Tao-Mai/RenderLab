#ifndef RENDERLAB_MATERIAL_SYSTEM_SLANG
#define RENDERLAB_MATERIAL_SYSTEM_SLANG

#include "Scene/Material/StandardMaterialData.slang"
#include "Scene/VertexData.slang"

struct MaterialSystem
{
    StructuredBuffer<StandardMaterialData> materialData;
    Texture2D<float4>                      materialTextures[kMaxMaterialTextures];
    SamplerState                           materialSamplers[kMaxMaterialSamplers];

    StandardMaterialData getMaterialData(const uint materialID)
    {
        return materialData[materialID];
    }

    float4 sampleTexture(const TextureRef texture, const float2 uv, const float4 uniformValue, const float lod = 0.f)
    {
        if (texture.textureIndex == kInvalidMaterialTexture)
            return uniformValue;

        return materialTextures[NonUniformResourceIndex(texture.textureIndex)].SampleLevel(
            materialSamplers[NonUniformResourceIndex(texture.samplerIndex)], uv, lod);
    }

    float3 evalEmissive(const uint materialID, const float2 uv)
    {
        const StandardMaterialData material = materialData[materialID];

        return sampleTexture(material.emissiveTexture, uv, float4(material.emissiveColor, 1.f)).rgb
            * material.emissiveFactor;
    }

    bool alphaTest(const float2 uv, const uint materialID)
    {
        const StandardMaterialData material = materialData[materialID];
        if (material.alphaMode != kAlphaModeMask)
            return false;

        const float alpha = sampleTexture(material.baseColorTexture, uv, material.baseColor).a;

        return alpha < material.alphaCutoff;
    }

    bool alphaTest(const VertexData v, const uint materialID)
    {
        return alphaTest(v.texC, materialID);
    }
};

#endif
