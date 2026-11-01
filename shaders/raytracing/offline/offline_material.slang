#ifndef RENDERLAB_OFFLINE_MATERIAL_SLANG
#define RENDERLAB_OFFLINE_MATERIAL_SLANG

#include "offline_texture.slang"

struct OfflineMaterialPreview
{
    OfflineMaterialPreviewModel model;
    uint firstTexture;
    uint secondTexture;
    uint instructionCount;
    float4 parameters;
};

[[vk::binding(8, 3)]] StructuredBuffer<OfflineMaterialPreview> materialInstructions;

float3 evaluateMaterialColor(OfflineMaterialPreview material, float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    float3 first = evaluateTexture(material.firstTexture, uv, position, normal, primitiveIndex);
    switch (material.model)
    {
        case OfflineMaterialPreviewModel::Color:
            return first;
        case OfflineMaterialPreviewModel::Conductor:
        {
            float3 k = evaluateTexture(material.secondTexture, uv, position, normal, primitiveIndex);
            return ((first - 1.0) * (first - 1.0) + k * k)
                / max((first + 1.0) * (first + 1.0) + k * k, float3(1e-8));
        }
        case OfflineMaterialPreviewModel::DiffuseTransmission:
        {
            float3 transmittance = evaluateTexture(material.secondTexture, uv, position, normal, primitiveIndex);
            return (first + transmittance) * material.parameters.x;
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
        OfflineMaterialPreview material = materialInstructions[program + index];
        float3 color;
        if (material.model == OfflineMaterialPreviewModel::Mix)
        {
            float amount = saturate(evaluateTexture(material.firstTexture, uv, position, normal, primitiveIndex).x);
            float3 second = colors[--depth];
            float3 first = colors[--depth];
            color = lerp(first, second, amount);
        }
        else color = evaluateMaterialColor(material, uv, position, normal, primitiveIndex);
        colors[depth++] = color;
    }
    return colors[0];
}

#endif
