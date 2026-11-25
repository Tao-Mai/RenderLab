#ifndef RENDERLAB_BRDF_SLANG
#define RENDERLAB_BRDF_SLANG

#include "render/impl/math/constant.slang"

// Keep the analytical-light GGX lobe finite at zero perceptual roughness (FP32).
static const float kMinDirectRoughness = 0.045;
static const float kMinBrdfDenominator = 1e-6;

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

#endif
