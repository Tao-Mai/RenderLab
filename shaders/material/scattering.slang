#ifndef RENDERLAB_MATERIAL_SCATTERING_SLANG
#define RENDERLAB_MATERIAL_SCATTERING_SLANG

#include "material/material_evaluation.slang"
#include "material/pbrt/material.slang"

StructuredBuffer<pbrt::MaterialInstruction> materialInstructions;
StructuredBuffer<pbrt::TextureInstruction> textureInstructions;

enum class ScatteringModel : uint
{
    Diffuse,
    Gltf,
    Conductor,
    SmoothDielectric,
};

struct ScatteringMaterial
{
    float4 coefficients0;
    float4 coefficients1;
    float roughness;
    ScatteringModel model;

    property PbrBrdf gltf
    {
        get
        {
            PbrBrdf brdf;
            brdf.diffuseColor = coefficients0.rgb;
            brdf.diffuseFresnelWeight = coefficients0.a;
            brdf.specularF0 = coefficients1.rgb;
            brdf.specularF90 = coefficients1.a;
            brdf.roughness = roughness;
            return brdf;
        }
    }

    property pbrt::Material pbrtMaterial
    {
        get
        {
            pbrt::Material material = {};
            if (model == ScatteringModel::Conductor)
            {
                material.model = pbrt::MaterialModel::Conductor;
                material.conductor.eta = coefficients0.rgb;
                material.conductor.k = coefficients1.rgb;
                material.conductor.alpha = float2(coefficients0.a, coefficients1.a);
            }
            else if (model == ScatteringModel::SmoothDielectric)
            {
                material.model = pbrt::MaterialModel::SmoothDielectric;
                material.eta = coefficients0.r;
            }
            else
            {
                material.model = pbrt::MaterialModel::Diffuse;
                material.diffuse.albedo = coefficients0.rgb;
            }
            return material;
        }
    }

    bool canTransmit() { return model == ScatteringModel::SmoothDielectric; }
    bool hasScattering() { return model == ScatteringModel::Gltf || pbrtMaterial.hasScattering(); }
    bool isDelta() { return model != ScatteringModel::Gltf && pbrtMaterial.isDelta(); }

    PbrBrdf rasterBrdf()
    {
        if (model == ScatteringModel::Gltf)
            return gltf;

        PbrBrdf result = {};
        pbrt::Material material = pbrtMaterial;
        if (model == ScatteringModel::Conductor)
        {
            result.specularF0 = pbrt::fresnelConductor(1.0, material.conductor.eta, material.conductor.k);
            result.specularF90 = 1.0;
            result.roughness = sqrt(saturate(0.5 * (material.conductor.alpha.x + material.conductor.alpha.y)));
        }
        else if (canTransmit())
        {
            float eta = max(material.eta, 1.0 / material.eta);
            result.specularF0 = float3(pbrt::fresnelDielectric(1.0, eta));
            result.specularF90 = 1.0;
        }
        else
        {
            result.diffuseColor = material.diffuse.albedo;
            result.roughness = 1.0;
        }
        return result;
    }
};

ScatteringMaterial evaluateScattering(
    MaterialData material, float2 uv, float3 position, float3 normal, uint primitiveIndex
)
{
    ScatteringMaterial result = {};
    if (material.kind == MaterialKind::Gltf)
    {
        PbrBrdf brdf = evaluatePbrMaterial(material, uv, RayMaterialSampler()).brdf;
        result.model = ScatteringModel::Gltf;
        result.coefficients0 = float4(brdf.diffuseColor, brdf.diffuseFresnelWeight);
        result.coefficients1 = float4(brdf.specularF0, brdf.specularF90);
        result.roughness = brdf.roughness;
        return result;
    }

    pbrt::Material evaluated = pbrt::evaluateMaterial(
        materialInstructions, textureInstructions, material.programIndex, uv, position, normal, primitiveIndex
    );
    if (evaluated.model == pbrt::MaterialModel::Conductor)
    {
        result.model = ScatteringModel::Conductor;
        result.coefficients0 = float4(evaluated.conductor.eta, evaluated.conductor.alpha.x);
        result.coefficients1 = float4(evaluated.conductor.k, evaluated.conductor.alpha.y);
    }
    else if (evaluated.model == pbrt::MaterialModel::SmoothDielectric)
    {
        result.model = ScatteringModel::SmoothDielectric;
        result.coefficients0 = float4(evaluated.eta, 0.0, 0.0, 0.0);
    }
    else
    {
        result.model = ScatteringModel::Diffuse;
        result.coefficients0 = float4(evaluated.diffuse.albedo, 0.0);
    }
    return result;
}

struct SurfaceBsdf
{
    ScatteringMaterial material;
    float3 normal;
    float3 tangent;
    float3 wo;
    bool frontFace;

    pbrt::Bsdf pbrtBsdf() { return pbrt::makeBsdf(material.pbrtMaterial, normal, tangent, wo, frontFace); }

    float3 evaluate(float3 wi)
    {
        if (material.model == ScatteringModel::Gltf)
            return material.gltf.evaluate(normal, wo, wi);
        return pbrtBsdf().evaluate(wi);
    }

    float pdf(float3 wi)
    {
        if (material.model == ScatteringModel::Gltf)
            return material.gltf.pdf(normal, wo, wi);
        return pbrtBsdf().pdf(wi);
    }

    pbrt::BsdfSample sample(float branchSample, float2 directionSample, pbrt::TransportMode mode)
    {
        if (material.model == ScatteringModel::Gltf)
        {
            BrdfSample sampled = material.gltf.sample(normal, wo, float3(directionSample, branchSample));
            return {sampled.direction, sampled.weight, sampled.pdf, 0};
        }
        return pbrtBsdf().sample(branchSample, directionSample, mode);
    }

    pbrt::BsdfSample sample(inout uint state)
    {
        if (material.model != ScatteringModel::Gltf)
            return pbrtBsdf().sample(state);

        float2 directionSample;
        directionSample.x = randomFloat(state);
        directionSample.y = randomFloat(state);
        return sample(randomFloat(state), directionSample, pbrt::TransportMode::Radiance);
    }
};

#endif
