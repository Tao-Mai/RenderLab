#ifndef RENDERLAB_OFFLINE_BRDF_SLANG
#define RENDERLAB_OFFLINE_BRDF_SLANG

#include "common/constant.slang"

enum class TransportMode
{
    Radiance,
    Importance,
};

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

// RGB form of the complex-index Fresnel equations.
float3 fresnelConductor(float cosine, float3 eta, float3 k)
{
    static const float kDenominatorFloor = 1e-20;
    float cos2 = saturate(cosine * cosine);
    float sin2 = 1.0 - cos2;
    float3 eta2 = eta * eta;
    float3 k2 = k * k;
    float3 difference = eta2 - k2 - sin2;
    float3 a2b2 = sqrt(difference * difference + 4.0 * eta2 * k2);
    float3 a = sqrt(max(0.0, 0.5 * (a2b2 + difference)));
    float3 twiceACos = 2.0 * a * abs(cosine);
    float3 perpendicular = (a2b2 + cos2 - twiceACos) / max(a2b2 + cos2 + twiceACos, kDenominatorFloor);
    float3 parallel = perpendicular * (cos2 * a2b2 + sin2 * sin2 - twiceACos * sin2)
        / max(cos2 * a2b2 + sin2 * sin2 + twiceACos * sin2, kDenominatorFloor);

    return saturate(0.5 * (parallel + perpendicular));
}

// PBRT-v4 conductor: anisotropic Trowbridge-Reitz distribution and visible-normal sampling.
// Directions are in the surface tangent frame, with the shading normal along +Z.
struct ConductorBrdf
{
    static const float kSmoothAlphaThreshold = 1e-3;
    static const float kMinimumRoughAlpha = 1e-4;
    static const float kVisibleNormalAxisThreshold = 0.99999;
    static const float kMinimumVisibleNormalZ = 1e-6;

    float3 eta;
    float3 k;
    float2 alpha;

    bool isDelta()
    {
        return max(alpha.x, alpha.y) < kSmoothAlphaThreshold;
    }

    [mutating]
    void setRoughness(float2 roughness, bool remap)
    {
        roughness = max(roughness, 0.0);
        alpha = remap ? sqrt(roughness) : roughness;
        if (!isDelta())
        {
            alpha = max(alpha, kMinimumRoughAlpha);
        }
    }

    float distribution(float3 halfDirection)
    {
        float3 scaled = float3(halfDirection.xy / alpha, halfDirection.z);
        float lengthSquared = dot(scaled, scaled);

        return 1.0 / (PI * alpha.x * alpha.y * lengthSquared * lengthSquared);
    }

    float lambda(float3 direction)
    {
        float3 scaled = float3(alpha * direction.xy, direction.z);

        return 0.5 * (length(scaled) / abs(direction.z) - 1.0);
    }

    float3 evaluate(float3 wo, float3 wi)
    {
        if (isDelta() || wo.z <= 0.0 || wi.z <= 0.0)
        {
            return float3(0.0);
        }

        float3 halfDirection = normalize(wo + wi);
        float maskingShadowing = 1.0 / (1.0 + lambda(wo) + lambda(wi));
        float3 fresnel = fresnelConductor(dot(wo, halfDirection), eta, k);

        return fresnel * distribution(halfDirection) * maskingShadowing / (4.0 * wo.z * wi.z);
    }

    float pdf(float3 wo, float3 wi)
    {
        if (isDelta() || wo.z <= 0.0 || wi.z <= 0.0)
        {
            return 0.0;
        }

        float3 halfDirection = normalize(wo + wi);
        float3 stretched = float3(alpha * wo.xy, wo.z);
        // VNDF density divided by the reflection Jacobian, with G1 / cos simplified.
        return distribution(halfDirection) / (2.0 * (wo.z + length(stretched)));
    }

    float3 sampleVisibleNormal(float3 wo, float2 sampleValue)
    {
        float3 stretched = normalize(float3(alpha * wo.xy, wo.z));
        float3 tangent = stretched.z < kVisibleNormalAxisThreshold
            ? normalize(cross(float3(0, 0, 1), stretched)) : float3(1, 0, 0);
        float3 bitangent = cross(stretched, tangent);
        float radius = sqrt(sampleValue.x);
        float phi = 2.0 * PI * sampleValue.y;
        float2 disk = radius * float2(cos(phi), sin(phi));
        disk.y = lerp(sqrt(max(0.0, 1.0 - disk.x * disk.x)), disk.y, 0.5 * (1.0 + stretched.z));
        float3 hemisphere = tangent * disk.x + bitangent * disk.y
            + stretched * sqrt(max(0.0, 1.0 - dot(disk, disk)));

        return normalize(float3(alpha * hemisphere.xy, max(hemisphere.z, kMinimumVisibleNormalZ)));
    }

    BrdfSample sample(float3 wo, float2 sampleValue)
    {
        BrdfSample result = {};
        if (wo.z <= 0.0)
        {
            return result;
        }
        if (isDelta())
        {
            result.direction = float3(-wo.xy, wo.z);
            result.weight = fresnelConductor(wo.z, eta, k);
            result.delta = 1;

            return result;
        }

        float3 halfDirection = sampleVisibleNormal(wo, sampleValue);
        float3 wi = reflect(-wo, halfDirection);
        // Below-surface samples contribute zero; resampling would change the PDF.
        if (wi.z <= 0.0)
        {
            return result;
        }
        result.direction = wi;
        result.pdf = pdf(wo, wi);
        float lambdaView = lambda(wo);
        result.weight = fresnelConductor(dot(wo, halfDirection), eta, k)
            * ((1.0 + lambdaView) / (1.0 + lambdaView + lambda(wi)));

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

    BrdfSample sample(float3 wo, float3 normal, float sampleValue, TransportMode mode)
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
        result.weight = mode == TransportMode::Radiance ? float3(etaIOverEtaT * etaIOverEtaT) : float3(1.0);

        return result;
    }
};

#endif
