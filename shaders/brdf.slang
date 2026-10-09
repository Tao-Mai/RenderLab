#include "constant.slang"

#ifndef ENABLE_CLEARCOAT
#error ENABLE_CLEARCOAT must be specified when compiling the scene shader
#endif

// Keep the analytical-light GGX lobe finite at zero perceptual roughness (FP32).
static const float kMinDirectRoughness = 0.045;
static const float kMinBrdfDenominator = 1e-6;

float3 fresnelSchlick(float3 f0, float vDotH)
{
    return f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
}

float normalDistributionGgx(float roughness, float nDotH)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float nDotHSquared = nDotH * nDotH;
    float denominator = (1.0 - nDotHSquared) + alphaSquared * nDotHSquared;
    return alphaSquared / (PI * denominator * denominator);
}

// Height-correlated Smith visibility: G / (4 * NdotV * NdotL).
float smithGgxVisibility(float roughness, float nDotV, float nDotL)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float lambdaV = sqrt(nDotV * nDotV + alphaSquared * (1.0 - nDotV * nDotV));
    float lambdaL = sqrt(nDotL * nDotL + alphaSquared * (1.0 - nDotL * nDotL));
    return 0.5 / max(nDotL * lambdaV + nDotV * lambdaL, kMinBrdfDenominator);
}

// Returns the BRDF only; the caller applies incident light, NdotL and shadow visibility.
float3 evaluateDirectBrdf(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 N,
    float3 V,
    float3 L
)
{
    float nDotV = saturate(dot(N, V));
    float nDotL = saturate(dot(N, L));
    if (nDotV <= 0.0 || nDotL <= 0.0)
        return float3(0.0);

    roughness = max(roughness, kMinDirectRoughness);
    float3 H = normalize(V + L);
    float nDotH = saturate(dot(N, H));
    float vDotH = saturate(dot(V, H));
    float3 f0 = lerp(float3(0.04), baseColor, metallic);
    float3 fresnel = fresnelSchlick(f0, vDotH);
    float distribution = normalDistributionGgx(roughness, nDotH);
    float visibility = smithGgxVisibility(roughness, nDotV, nDotL);

    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * baseColor * INV_PI;
    float3 specular = fresnel * distribution * visibility;
    return diffuse + specular;
}

#if ENABLE_CLEARCOAT
// Air / clearcoat interface with IOR 1.5; the base material keeps its own F0.
static const float kClearcoatF0 = 0.04;

float kelemenVisibility(float lDotH)
{
    return 0.25 / max(lDotH * lDotH, kMinBrdfDenominator);
}

// GGX D * Kelemen V only. The caller applies the glTF Fresnel layer weight and
// this layer's own NdotL, rather than the base normal's cosine.
float evaluateClearcoatBrdf(float roughness, float3 normal, float3 V, float3 L)
{
    if (dot(normal, V) <= 0.0 || dot(normal, L) <= 0.0)
        return 0.0;

    roughness = max(roughness, kMinDirectRoughness);
    float3 H = normalize(V + L);
    float nDotH = saturate(dot(normal, H));
    float lDotH = saturate(dot(L, H));
    return normalDistributionGgx(roughness, nDotH) * kelemenVisibility(lDotH);
}
#endif
