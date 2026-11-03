#ifndef RENDERLAB_MATERIAL_EVALUATION_SLANG
#define RENDERLAB_MATERIAL_EVALUATION_SLANG

#include "common/material_data.slang"
#include "common/pbr_brdf.slang"

interface IMaterialTextureSampler
{
    float4 sample(TextureRef texture, float2 uv);
}

struct RasterMaterialSampler : IMaterialTextureSampler
{
    float4 sample(TextureRef texture, float2 uv)
    {
        return sampleTexture(texture, uv);
    }
};

struct RayMaterialSampler : IMaterialTextureSampler
{
    float4 sample(TextureRef texture, float2 uv)
    {
        return sampleTextureLevel(texture, uv);
    }
};

struct EvaluatedPbrMaterial
{
    PbrBrdf brdf;
    float opacity;
};

EvaluatedPbrMaterial evaluatePbrMaterial<TSampler>(MaterialData material, float2 uv, TSampler sampler)
    where TSampler : IMaterialTextureSampler
{
    float4 colorSample = sampler.sample(material.colorTexture, uv);
    float3 color = colorSample.rgb * material.color;
    EvaluatedPbrMaterial result;
    result.opacity = colorSample.a * material.opacityFactor;

    if (material.workflow == kMaterialWorkflowSpecularGlossiness)
    {
        // The sRGB image view decodes RGB while leaving glossiness in A linear.
        float4 specularGlossiness = sampler.sample(material.specularColorTexture, uv);
        float3 specular = saturate(material.specularColorFactor * specularGlossiness.rgb);
        float glossiness = saturate(material.glossinessFactor * specularGlossiness.a);
        result.brdf.diffuseColor = color * (1.0 - max(specular.x, max(specular.y, specular.z)));
        result.brdf.specularF0 = specular;
        result.brdf.specularF90 = 1.0;
        result.brdf.roughness = 1.0 - glossiness;
        result.brdf.diffuseFresnelWeight = 0.0;
    }
    else
    {
        float4 metallicSample = sampler.sample(material.metallicTexture, uv);
        float4 roughnessSample = metallicSample;
        if (material.metallicTexture.textureIndex != material.roughnessTexture.textureIndex ||
            material.metallicTexture.samplerIndex != material.roughnessTexture.samplerIndex)
        {
            roughnessSample = sampler.sample(material.roughnessTexture, uv);
        }
        float metallic = saturate(material.metallic * metallicSample.b);
        float roughness = saturate(material.roughness * roughnessSample.g);
        float3 specularColor = material.specularColorFactor * sampler.sample(material.specularColorTexture, uv).rgb;
        float specularWeight = saturate(material.specularFactor * sampler.sample(material.specularTexture, uv).a);
        float3 dielectricF0 = min(material.dielectricF0 * specularColor, float3(1.0)) * specularWeight;
        float maximumF0 = max(dielectricF0.x, max(dielectricF0.y, dielectricF0.z));

        result.brdf.diffuseColor = color * (1.0 - metallic) * (1.0 - maximumF0);
        result.brdf.specularF0 = lerp(dielectricF0, color, metallic);
        result.brdf.specularF90 = lerp(specularWeight, 1.0, metallic);
        result.brdf.roughness = roughness;
        result.brdf.diffuseFresnelWeight = maximumF0 < 1.0
            ? (specularWeight - maximumF0) / (1.0 - maximumF0) : 0.0;
    }

    return result;
}

#endif
