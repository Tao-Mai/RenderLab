#ifndef RENDERLAB_OFFLINE_MATERIAL_SLANG
#define RENDERLAB_OFFLINE_MATERIAL_SLANG

#include "material/pbrt/bsdf.slang"
#include "math/random.slang"
#include "texture.slang"

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

pbrt::ConductorBrdf evaluateConductor(
    MaterialPreview material, float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    static const float kMaximumReflectance = 0.9999;
    pbrt::ConductorBrdf brdf;
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

float3 evaluateDiffuseColor(uint program, uint instructionCount, float2 uv, float3 position, float3 normal,
    uint primitiveIndex)
{
    float3 colors[kOfflineMaterialStackSize];
    uint depth = 0;
    for (uint index = 1; index <= instructionCount; ++index)
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

struct OfflineMaterial
{
    OfflineMaterialPreviewModel model;
    pbrt::DiffuseBrdf diffuse;
    pbrt::ConductorBrdf conductor;
    float eta;

    bool hasScattering() { return model != OfflineMaterialPreviewModel::Diffuse || any(diffuse.albedo > 0.0); }
    bool canTransmit() { return model == OfflineMaterialPreviewModel::SmoothDielectric; }

    bool isDelta()
    {
        return model == OfflineMaterialPreviewModel::SmoothDielectric ||
            (model == OfflineMaterialPreviewModel::Conductor && conductor.isDelta());
    }
};

OfflineMaterial evaluateOfflineMaterial(uint index, float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    MaterialPreview header = materialInstructions[index];
    OfflineMaterial result = {};
    result.model = header.model;
    if (header.model == OfflineMaterialPreviewModel::SmoothDielectric)
    {
        static const float kMinimumEta = 1e-4;
        float3 eta = evaluateTexture(header.firstTexture, uv, position, normal, primitiveIndex);
        result.eta = max((eta.x + eta.y + eta.z) / 3.0, kMinimumEta);
    }
    else if (header.model == OfflineMaterialPreviewModel::Conductor)
    {
        result.conductor = evaluateConductor(header, uv, position, normal, primitiveIndex);
    }
    else
    {
        result.diffuse.albedo = saturate(evaluateDiffuseColor(index, header.instructionCount, uv, position, normal,
            primitiveIndex));
    }

    return result;
}

// Scattering at a surface point. The integrator applies geometry and transport corrections.
struct OfflineBsdf
{
    OfflineMaterial material;
    float3 normal;
    float3 tangent;
    float3 bitangent;
    float3 wo;
    pbrt::SmoothDielectricBsdf dielectric;

    float3 toLocal(float3 direction)
    {
        return float3(dot(tangent, direction), dot(bitangent, direction), dot(normal, direction));
    }

    float3 evaluate(float3 wi)
    {
        if (material.model == OfflineMaterialPreviewModel::Conductor)
        {
            return material.conductor.evaluate(toLocal(wo), toLocal(wi));
        }

        return material.model == OfflineMaterialPreviewModel::SmoothDielectric
            ? float3(0.0) : material.diffuse.evaluate();
    }

    float pdf(float3 wi)
    {
        if (material.model == OfflineMaterialPreviewModel::Conductor)
        {
            return material.conductor.pdf(toLocal(wo), toLocal(wi));
        }

        return material.model == OfflineMaterialPreviewModel::SmoothDielectric ? 0.0 : material.diffuse.pdf(normal, wi);
    }

    pbrt::BsdfSample sampleReflection(float2 directionSample)
    {
        if (material.model == OfflineMaterialPreviewModel::Conductor)
        {
            pbrt::BsdfSample result = material.conductor.sample(toLocal(wo), directionSample);
            result.direction = tangent * result.direction.x + bitangent * result.direction.y + normal * result.direction.z;

            return result;
        }

        return material.diffuse.sample(normal, directionSample);
    }

    pbrt::BsdfSample sample(float branchSample, float2 directionSample, pbrt::TransportMode mode)
    {
        if (material.model == OfflineMaterialPreviewModel::SmoothDielectric)
        {
            return dielectric.sample(wo, normal, branchSample, mode);
        }

        return sampleReflection(directionSample);
    }

    pbrt::BsdfSample sample(inout uint state)
    {
        if (material.model == OfflineMaterialPreviewModel::SmoothDielectric)
        {
            return dielectric.sample(wo, normal, randomFloat(state), pbrt::TransportMode::Radiance);
        }

        float2 directionSample;
        directionSample.x = randomFloat(state);
        directionSample.y = randomFloat(state);

        return sampleReflection(directionSample);
    }
};

OfflineBsdf makeOfflineBsdf(OfflineMaterial material, float3 normal, float3 tangent, float3 wo, bool frontFace)
{
    OfflineBsdf bsdf = {};
    bsdf.material = material;
    bsdf.normal = normal;
    bsdf.wo = wo;
    if (material.model == OfflineMaterialPreviewModel::SmoothDielectric)
    {
        bsdf.dielectric.eta = frontFace ? material.eta : 1.0 / material.eta;
    }
    else if (material.model == OfflineMaterialPreviewModel::Conductor)
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

#endif
