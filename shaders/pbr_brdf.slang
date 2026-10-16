#ifndef RENDERLAB_PBR_BRDF_SLANG
#define RENDERLAB_PBR_BRDF_SLANG

#include "brdf.slang"

struct BrdfSample
{
    float3 direction;
    float3 weight; // BRDF * NdotL / the complete mixture PDF.
    float pdf;
};

// Isotropic GGX visible-normal sampling in a local frame with N = +Z.
// Heitz VNDF: stretch the view, warp a disk onto its visible hemisphere, then unstretch.
// https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory
float3 sampleGgxVisibleNormal(float3 viewDirection, float roughness, float2 sample)
{
    float alpha = roughness * roughness;
    float3 stretchedView = normalize(float3(alpha * viewDirection.xy, viewDirection.z));
    float3 tangent = stretchedView.z < 0.99999
        ? normalize(cross(float3(0.0, 0.0, 1.0), stretchedView)) : float3(1.0, 0.0, 0.0);
    float3 bitangent = cross(stretchedView, tangent);

    float radius = sqrt(sample.x);
    float phi = 2.0 * PI * sample.y;
    float2 disk = radius * float2(cos(phi), sin(phi));
    float blend = 0.5 * (1.0 + stretchedView.z);
    disk.y = lerp(sqrt(max(0.0, 1.0 - disk.x * disk.x)), disk.y, blend);
    float3 hemisphere = tangent * disk.x + bitangent * disk.y
        + stretchedView * sqrt(max(0.0, 1.0 - dot(disk, disk)));
    return normalize(float3(alpha * hemisphere.xy, max(hemisphere.z, 1e-6)));
}

struct PbrBrdf
{
    float3 baseColor;
    float metallic;
    float roughness;

    float3 evaluate(float3 normal, float3 viewDirection, float3 lightDirection)
    {
        return evaluateDirectBrdf(baseColor, metallic, roughness, normal, viewDirection, lightDirection);
    }

    float specularProbability(float nDotV)
    {
        float3 f0 = lerp(float3(0.04), baseColor, metallic);
        float3 fresnel = fresnelSchlick(f0, nDotV);
        float specularWeight = max(fresnel.x, max(fresnel.y, fresnel.z));
        float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * baseColor;
        float diffuseWeight = max(diffuse.x, max(diffuse.y, diffuse.z));
        float total = specularWeight + diffuseWeight;
        return total > 0.0 ? specularWeight / total : 1.0;
    }

    float pdf(float3 normal, float3 viewDirection, float3 lightDirection)
    {
        float nDotV = saturate(dot(normal, viewDirection));
        float nDotL = saturate(dot(normal, lightDirection));
        if (nDotV <= 0.0 || nDotL <= 0.0)
            return 0.0;

        float3 halfDirection = normalize(viewDirection + lightDirection);
        float samplingRoughness = max(roughness, kMinDirectRoughness);
        float alpha = samplingRoughness * samplingRoughness;
        // Visible-normal PDF / reflection Jacobian: D(H) * G1(V) / (4 * NdotV).
        float smithRoot = sqrt(alpha * alpha + (1.0 - alpha * alpha) * nDotV * nDotV);
        float specularPdf = normalDistributionGgx(samplingRoughness, saturate(dot(normal, halfDirection)))
            / (2.0 * (nDotV + smithRoot));
        return lerp(nDotL * INV_PI, specularPdf, specularProbability(nDotV));
    }

    BrdfSample sample(float3 normal, float3 viewDirection, float3 randomSample)
    {
        BrdfSample result = {float3(0.0), float3(0.0), 0.0};
        float nDotV = dot(normal, viewDirection);
        if (nDotV <= 0.0)
            return result;

        float3 axis = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        float3 tangent = normalize(cross(axis, normal));
        float3 bitangent = cross(normal, tangent);
        float3 localView = float3(dot(viewDirection, tangent), dot(viewDirection, bitangent), nDotV);
        float3 localDirection;
        if (randomSample.z < specularProbability(nDotV))
        {
            float3 halfDirection = sampleGgxVisibleNormal(
                localView, max(roughness, kMinDirectRoughness), randomSample.xy);
            localDirection = reflect(-localView, halfDirection);
        }
        else
        {
            float radius = sqrt(randomSample.x);
            float phi = 2.0 * PI * randomSample.y;
            localDirection = float3(radius * cos(phi), radius * sin(phi), sqrt(1.0 - randomSample.x));
        }

        // Do not resample below-surface GGX reflections: they are zero-contribution samples.
        if (localDirection.z <= 0.0)
            return result;

        result.direction = normalize(tangent * localDirection.x + bitangent * localDirection.y
            + normal * localDirection.z);
        result.pdf = pdf(normal, viewDirection, result.direction);
        if (result.pdf > 0.0)
            result.weight = evaluate(normal, viewDirection, result.direction)
                * saturate(dot(normal, result.direction)) / result.pdf;
        return result;
    }
};

#endif
