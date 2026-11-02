#ifndef RENDERLAB_OFFLINE_MATERIAL_SLANG
#define RENDERLAB_OFFLINE_MATERIAL_SLANG

#include "raytracing/offline/texture.slang"
#include "raytracing/offline/brdf.slang"

struct MaterialPreview
{
    OfflineMaterialPreviewModel model;
    uint firstTexture;
    uint secondTexture;
    uint instructionCount;
    uint4 inputs;

    property roughnessUTexture : uint {
        get { return inputs.x; }
    }

    property roughnessVTexture : uint {
        get { return inputs.y; }
    }

    property remapRoughness : bool {
        get { return inputs.z != 0; }
    }

    property usesReflectance : bool {
        get { return inputs.w != 0; }
    }

    property scaleTexture : uint {
        get { return inputs.x; }
    }
};

[[vk::binding(OfflineRayTracingBinding::kMaterials, 3)]] StructuredBuffer<MaterialPreview> materialInstructions;

ConductorBrdf evaluateConductor(MaterialPreview material, float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    static const float kMaximumReflectance = 0.9999;
    ConductorBrdf brdf;
    float3 first = evaluateTexture(material.firstTexture, uv, position, normal, primitiveIndex);
    if (material.usesReflectance)
    {
        float3 reflectance = clamp(first, 0.0, kMaximumReflectance);
        brdf.eta = float3(1.0);
        brdf.k = 2.0 * sqrt(reflectance / (1.0 - reflectance));
    }
    else
    {
        brdf.eta = max(first, 0.0);
        brdf.k = max(evaluateTexture(material.secondTexture, uv, position, normal, primitiveIndex), 0.0);
    }

    float2 roughness;
    roughness.x = evaluateTexture(material.roughnessUTexture, uv, position, normal, primitiveIndex).x;
    roughness.y = evaluateTexture(material.roughnessVTexture, uv, position, normal, primitiveIndex).x;
    brdf.setRoughness(roughness, material.remapRoughness);

    return brdf;
}

float3 evaluateMaterialColor(MaterialPreview material, float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    float3 first = evaluateTexture(material.firstTexture, uv, position, normal, primitiveIndex);
    switch (material.model)
    {
        case OfflineMaterialPreviewModel::Color:

            return first;
        case OfflineMaterialPreviewModel::ConductorColor:
        {
            float3 k = evaluateTexture(material.secondTexture, uv, position, normal, primitiveIndex);

            return ((first - 1.0) * (first - 1.0) + k * k)
                / max((first + 1.0) * (first + 1.0) + k * k, float3(1e-8));
        }
        case OfflineMaterialPreviewModel::DiffuseTransmission:
        {
            float3 transmittance = evaluateTexture(material.secondTexture, uv, position, normal, primitiveIndex);
            float scale = evaluateTexture(material.scaleTexture, uv, position, normal, primitiveIndex).x;

            return (first + transmittance) * scale;
        }
        case OfflineMaterialPreviewModel::Subsurface:
        {
            float3 sigmaS = evaluateTexture(material.secondTexture, uv, position, normal, primitiveIndex);

            return sigmaS / max(first + sigmaS, float3(1e-8));
        }
        case OfflineMaterialPreviewModel::HairAbsorption:

            return exp(-first);
        case OfflineMaterialPreviewModel::HairPigments:
        {
            float3 pheomelanin = evaluateTexture(material.secondTexture, uv, position, normal, primitiveIndex);

            return exp(-first * float3(0.419, 0.697, 1.37) - pheomelanin * float3(0.187, 0.4, 1.05));
        }
    }

    return first;
}

float3 evaluateMaterial(uint program, float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    float3 colors[kOfflineMaterialStackSize];
    uint depth = 0;
    uint count = materialInstructions[program].instructionCount;
    for (uint index = 1; index <= count; ++index)
    {
        MaterialPreview material = materialInstructions[program + index];
        float3 color;
        if (material.model == OfflineMaterialPreviewModel::Mix)
        {
            float amount = saturate(evaluateTexture(material.firstTexture, uv, position, normal, primitiveIndex).x);
            float3 second = colors[--depth];
            float3 first = colors[--depth];
            color = lerp(first, second, amount);
        }
        else
        {
            color = evaluateMaterialColor(material, uv, position, normal, primitiveIndex);
        }
        colors[depth++] = color;
    }

    return colors[0];
}

#endif
