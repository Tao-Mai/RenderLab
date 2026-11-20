#ifndef RENDERLAB_PBRT_MATERIAL_SLANG
#define RENDERLAB_PBRT_MATERIAL_SLANG

#include "material/pbrt/bsdf.slang"
#include "math/random.slang"
#include "material/pbrt/texture.slang"

namespace pbrt
{
struct MaterialInstruction
{
    MaterialModel model;
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

ConductorBrdf evaluateConductor(
    StructuredBuffer<TextureInstruction> textureInstructions, MaterialInstruction material,
    float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    static const float kMaximumReflectance = 0.9999;
    ConductorBrdf brdf;
    float3 first = evaluateTexture(textureInstructions, material.firstTexture, uv, position, normal, primitiveIndex);
    if (material.usesReflectance)
    {
        float3 reflectance = clamp(first, 0.0, kMaximumReflectance);
        brdf.eta = float3(1.0);
        brdf.k = 2.0 * sqrt(reflectance / (1.0 - reflectance));
    }
    else
    {
        brdf.eta = max(first, 0.0);
        brdf.k = max(evaluateTexture(textureInstructions, material.secondTexture, uv, position, normal, primitiveIndex), 0.0);
    }

    float2 roughness;
    roughness.x = evaluateTexture(textureInstructions, material.roughnessUTexture, uv, position, normal, primitiveIndex).x;
    roughness.y = evaluateTexture(textureInstructions, material.roughnessVTexture, uv, position, normal, primitiveIndex).x;
    brdf.setRoughness(roughness, material.remapRoughness);

    return brdf;
}

float3 evaluateMaterialColor(StructuredBuffer<TextureInstruction> textureInstructions, MaterialInstruction material,
    float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    float3 first = evaluateTexture(textureInstructions, material.firstTexture, uv, position, normal, primitiveIndex);
    switch (material.model)
    {
        case MaterialModel::Color:

            return first;
        case MaterialModel::ConductorColor:
        {
            float3 k = evaluateTexture(textureInstructions, material.secondTexture, uv, position, normal, primitiveIndex);

            return ((first - 1.0) * (first - 1.0) + k * k)
                / max((first + 1.0) * (first + 1.0) + k * k, float3(1e-8));
        }
        case MaterialModel::DiffuseTransmission:
        {
            float3 transmittance = evaluateTexture(textureInstructions, material.secondTexture, uv, position, normal, primitiveIndex);
            float scale = evaluateTexture(textureInstructions, material.scaleTexture, uv, position, normal, primitiveIndex).x;

            return (first + transmittance) * scale;
        }
        case MaterialModel::Subsurface:
        {
            float3 sigmaS = evaluateTexture(textureInstructions, material.secondTexture, uv, position, normal, primitiveIndex);

            return sigmaS / max(first + sigmaS, float3(1e-8));
        }
        case MaterialModel::HairAbsorption:

            return exp(-first);
        case MaterialModel::HairPigments:
        {
            float3 pheomelanin = evaluateTexture(textureInstructions, material.secondTexture, uv, position, normal, primitiveIndex);

            return exp(-first * float3(0.419, 0.697, 1.37) - pheomelanin * float3(0.187, 0.4, 1.05));
        }
    }

    return first;
}

float3 evaluateDiffuseColor(StructuredBuffer<MaterialInstruction> materialInstructions,
    StructuredBuffer<TextureInstruction> textureInstructions, uint program, uint instructionCount,
    float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    float3 colors[kMaterialStackSize];
    uint depth = 0;
    for (uint index = 1; index <= instructionCount; ++index)
    {
        MaterialInstruction material = materialInstructions[program + index];
        float3 color;
        if (material.model == MaterialModel::Mix)
        {
            float amount = saturate(evaluateTexture(textureInstructions, material.firstTexture, uv, position, normal, primitiveIndex).x);
            float3 second = colors[--depth];
            float3 first = colors[--depth];
            color = lerp(first, second, amount);
        }
        else
        {
            color = evaluateMaterialColor(textureInstructions, material, uv, position, normal, primitiveIndex);
        }
        colors[depth++] = color;
    }

    return colors[0];
}

struct Material
{
    MaterialModel model;
    DiffuseBrdf diffuse;
    ConductorBrdf conductor;
    float eta;

    bool hasScattering() { return model != MaterialModel::Diffuse || any(diffuse.albedo > 0.0); }
    bool canTransmit() { return model == MaterialModel::SmoothDielectric; }

    bool isDelta()
    {
        return model == MaterialModel::SmoothDielectric ||
            (model == MaterialModel::Conductor && conductor.isDelta());
    }
};

Material evaluateMaterial(StructuredBuffer<MaterialInstruction> materialInstructions,
    StructuredBuffer<TextureInstruction> textureInstructions, uint index, float2 uv, float3 position,
    float3 normal, uint primitiveIndex)
{
    MaterialInstruction header = materialInstructions[index];
    Material result = {};
    result.model = header.model;
    if (header.model == MaterialModel::SmoothDielectric)
    {
        static const float kMinimumEta = 1e-4;
        float3 eta = evaluateTexture(textureInstructions, header.firstTexture, uv, position, normal, primitiveIndex);
        result.eta = max((eta.x + eta.y + eta.z) / 3.0, kMinimumEta);
    }
    else if (header.model == MaterialModel::Conductor)
    {
        result.conductor = evaluateConductor(textureInstructions, header, uv, position, normal, primitiveIndex);
    }
    else
    {
        result.diffuse.albedo = saturate(evaluateDiffuseColor(materialInstructions, textureInstructions, index,
            header.instructionCount, uv, position, normal, primitiveIndex));
    }

    return result;
}

// Scattering at a surface point. The integrator applies geometry and transport corrections.
struct Bsdf
{
    Material material;
    float3 normal;
    float3 tangent;
    float3 bitangent;
    float3 wo;
    SmoothDielectricBsdf dielectric;

    float3 toLocal(float3 direction)
    {
        return float3(dot(tangent, direction), dot(bitangent, direction), dot(normal, direction));
    }

    float3 evaluate(float3 wi)
    {
        if (material.model == MaterialModel::Conductor)
        {
            return material.conductor.evaluate(toLocal(wo), toLocal(wi));
        }

        return material.model == MaterialModel::SmoothDielectric
            ? float3(0.0) : material.diffuse.evaluate();
    }

    float pdf(float3 wi)
    {
        if (material.model == MaterialModel::Conductor)
        {
            return material.conductor.pdf(toLocal(wo), toLocal(wi));
        }

        return material.model == MaterialModel::SmoothDielectric ? 0.0 : material.diffuse.pdf(normal, wi);
    }

    BsdfSample sampleReflection(float2 directionSample)
    {
        if (material.model == MaterialModel::Conductor)
        {
            BsdfSample result = material.conductor.sample(toLocal(wo), directionSample);
            result.direction = tangent * result.direction.x + bitangent * result.direction.y + normal * result.direction.z;

            return result;
        }

        return material.diffuse.sample(normal, directionSample);
    }

    BsdfSample sample(float branchSample, float2 directionSample, TransportMode mode)
    {
        if (material.model == MaterialModel::SmoothDielectric)
        {
            return dielectric.sample(wo, normal, branchSample, mode);
        }

        return sampleReflection(directionSample);
    }

    BsdfSample sample(inout uint state)
    {
        if (material.model == MaterialModel::SmoothDielectric)
        {
            return dielectric.sample(wo, normal, randomFloat(state), TransportMode::Radiance);
        }

        float2 directionSample;
        directionSample.x = randomFloat(state);
        directionSample.y = randomFloat(state);

        return sampleReflection(directionSample);
    }
};

Bsdf makeBsdf(Material material, float3 normal, float3 tangent, float3 wo, bool frontFace)
{
    Bsdf bsdf = {};
    bsdf.material = material;
    bsdf.normal = normal;
    bsdf.wo = wo;
    if (material.model == MaterialModel::SmoothDielectric)
    {
        bsdf.dielectric.eta = frontFace ? material.eta : 1.0 / material.eta;
    }
    else if (material.model == MaterialModel::Conductor)
    {
        tangent -= normal * dot(normal, tangent);
        if (dot(tangent, tangent) == 0.0)
        {
            static const float kPoleAlignment = 0.999;
            float3 axis = abs(normal.z) < kPoleAlignment ? float3(0, 0, 1) : float3(0, 1, 0);
            tangent = cross(axis, normal);
        }
        bsdf.tangent = normalize(tangent);
        bsdf.bitangent = cross(normal, bsdf.tangent);
    }

    return bsdf;
}

}

#endif
