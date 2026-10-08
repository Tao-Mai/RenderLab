#include "constant.slang"

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
    float3 normal,
    float3 viewDirection,
    float3 lightDirection)
{
    float nDotV = saturate(dot(normal, viewDirection));
    float nDotL = saturate(dot(normal, lightDirection));
    if (nDotV <= 0.0 || nDotL <= 0.0)
        return float3(0.0);

    roughness = max(roughness, kMinDirectRoughness);
    float3 halfDirection = normalize(viewDirection + lightDirection);
    float nDotH = saturate(dot(normal, halfDirection));
    float vDotH = saturate(dot(viewDirection, halfDirection));
    float3 f0 = lerp(float3(0.04), baseColor, metallic);
    float3 fresnel = fresnelSchlick(f0, vDotH);
    float distribution = normalDistributionGgx(roughness, nDotH);
    float visibility = smithGgxVisibility(roughness, nDotV, nDotL);

    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * baseColor * INV_PI;
    float3 specular = fresnel * distribution * visibility;
    return diffuse + specular;
}

