#ifndef RENDERLAB_OFFLINE_BRDF_SLANG
#define RENDERLAB_OFFLINE_BRDF_SLANG

#include "common/constant.slang"

struct BrdfSample
{
    float3 direction;
    float3 weight;
    float pdf;
    uint delta; // 是否镜面散射
};

struct DiffuseBrdf
{
    float3 albedo;

    float3 evaluate()
    {
        return albedo * INV_PI;
    }

    float pdf(float3 normal, float3 direction)
    {
        return max(0.0, dot(normal, direction)) * INV_PI;
    }

    BrdfSample sample(float3 normal, float2 sampleValue)
    {
        float radius = sqrt(sampleValue.x);
        float phi = 2.0 * PI * sampleValue.y;
        float3 local = float3(radius * cos(phi), radius * sin(phi), sqrt(1.0 - sampleValue.x));
        float3 axis = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
        float3 tangent = normalize(cross(axis, normal));
        float3 bitangent = cross(normal, tangent);

        BrdfSample result;
        result.direction = normalize(tangent * local.x + bitangent * local.y + normal * local.z);
        result.weight = albedo;
        result.pdf = pdf(normal, result.direction);
        result.delta = 0;
        return result;
    }
};

float fresnelDielectric(float cosThetaI, float eta)
{
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    if (cosThetaI < 0.0)
    {
        eta = 1.0 / eta;
        cosThetaI = -cosThetaI;
    }

    float sin2ThetaI = max(0.0, 1.0 - cosThetaI * cosThetaI);
    float sin2ThetaT = sin2ThetaI / (eta * eta);
    if (sin2ThetaT >= 1.0)
    {
        return 1.0;
    }

    float cosThetaT = sqrt(max(0.0, 1.0 - sin2ThetaT));
    float parallel = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float perpendicular = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);
    return 0.5 * (parallel * parallel + perpendicular * perpendicular);
}

struct SmoothDielectricBsdf
{
    float eta;

    BrdfSample sample(float3 wo, float3 normal, float sampleValue)
    {
        BrdfSample result;
        result.direction = float3(0.0);
        result.weight = float3(0.0);
        result.pdf = 0.0;
        result.delta = 1;

        float cosThetaI = dot(wo, normal);
        if (cosThetaI <= 0.0)
        {
            return result;
        }

        float reflectance = fresnelDielectric(cosThetaI, eta);
        float transmittance = 1.0 - reflectance;
        if (reflectance == 0.0 && transmittance == 0.0)
        {
            return result;
        }

        if (sampleValue < reflectance / (reflectance + transmittance))
        {
            result.direction = reflect(-wo, normal);
            result.weight = float3(1.0);
            return result;
        }

        float etaIOverEtaT = 1.0 / eta;
        float sin2ThetaT = etaIOverEtaT * etaIOverEtaT * max(0.0, 1.0 - cosThetaI * cosThetaI);
        if (sin2ThetaT >= 1.0)
        {
            result.direction = reflect(-wo, normal);
            result.weight = float3(1.0);
            return result;
        }

        float cosThetaT = sqrt(max(0.0, 1.0 - sin2ThetaT));
        result.direction = normalize(-wo * etaIOverEtaT + normal * (etaIOverEtaT * cosThetaI - cosThetaT));
        result.weight = float3(etaIOverEtaT * etaIOverEtaT);
        return result;
    }
};

#endif
