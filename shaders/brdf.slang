#include "constant.slang"

float3 FresnelSchlick(float metallic, float3 baseColor, float VoH) {
    float3 F0 = lerp(0.04, baseColor, float3(metallic));
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

float NormalDistributionGGX(float roughness, float NdotH) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-5);
}

float GeometrySmithGGX(float roughness, float NdotV, float NdotL)
{
    float a = roughness * roughness; // alpha = r²
    float a2 = a * a;                // alpha²

    float lambdaV = sqrt(NdotV * NdotV + a2 * (1.0 - NdotV * NdotV));
    float lambdaL = sqrt(NdotL * NdotL + a2 * (1.0 - NdotL * NdotL));

    return 2.0 * NdotV * NdotL / max(NdotL * lambdaV + NdotV * lambdaL, 1e-5);
}

