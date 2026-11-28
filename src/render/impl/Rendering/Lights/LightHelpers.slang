#ifndef RENDERLAB_LIGHT_HELPERS_SLANG
#define RENDERLAB_LIGHT_HELPERS_SLANG

#include "Scene/Lights/LightData.slang"
#include "Utils/Math/MathConstants.slangh"
#include "Utils/Sampling/SampleGeneratorInterface.slang"

static const float kMinLightDistSqr  = 1e-9f;
static const float kMaxLightDistance = FLT_MAX;

struct AnalyticLightSample
{
    float3 posW;      // Sampled point on the light source in world space (for local lights only).
    float3 normalW;   // Normal of the sampled point on the light source in world space (normalized).
    float3 dir;       // Direction from the shading point to the sampled point on the light in world space (normalized).
    float  distance;  // Distance from the shading point to sampled point on the light.
    float3 Li;        // Incident radiance at the shading point (unshadowed). Note: Already divided by the pdf.
    float  pdf;       // Probability density function with respect to solid angle at the shading point.
};

bool finalizeAreaLightSample(const float3 shadingPosW, const LightData light, inout AnalyticLightSample ls)
{
    float3 toLight = ls.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;

    // Area lights are single-sided, so radiance is zero when seen from the back-facing side.
    float cosTheta = dot(ls.normalW, -ls.dir);
    if (cosTheta <= 0.f)
        return false;

    ls.Li = light.intensity * (light.surfaceArea * cosTheta / distSqr);
    ls.pdf = distSqr / (cosTheta * light.surfaceArea);

    return true;
}

bool sampleRectAreaLight(const float3 shadingPosW, const LightData light, const float2 u, out AnalyticLightSample ls)
{
    ls = {};
    float3 pos = float3(u.x * 2.f - 1.f, u.y * 2.f - 1.f, 0.f);
    ls.posW = mul(light.transMat, float4(pos, 1.f)).xyz;
    ls.normalW = normalize(mul(light.transMatIT, float4(0.f, 0.f, 1.f, 0.f)).xyz);

    return finalizeAreaLightSample(shadingPosW, light, ls);
}

bool sampleDirectionalLight(const float3 shadingPosW, const LightData light, out AnalyticLightSample ls)
{
    ls = {};
    ls.posW = float3(0.f, 0.f, 0.f);
    ls.normalW = light.dirW;
    ls.distance = kMaxLightDistance;
    ls.dir = -light.dirW;
    ls.Li = light.intensity;
    ls.pdf = 0.f;

    return true;
}

bool samplePointLight(const float3 shadingPosW, const LightData light, out AnalyticLightSample ls)
{
    ls = {};
    ls.posW = light.posW;
    ls.normalW = light.dirW;

    float3 toLight = ls.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;

    float cosTheta = -dot(ls.dir, light.dirW);
    float falloff = 1.f;
    if (cosTheta < light.cosOpeningAngle)
    {
        falloff = 0.f;
    }
    else if (light.penumbraAngle > 0.f)
    {
        float deltaAngle = light.openingAngle - acos(clamp(cosTheta, -1.f, 1.f));
        falloff = smoothstep(0.f, light.penumbraAngle, deltaAngle);
    }

    ls.Li = light.intensity * (falloff / distSqr);
    ls.pdf = 0.f;

    return true;
}

bool sampleLight<S : ISampleGenerator>(
    const float3 shadingPosW, const LightData light, inout S sg, out AnalyticLightSample ls)
{
    switch (LightType(light.type))
    {
    case LightType::Point:
        return samplePointLight(shadingPosW, light, ls);
    case LightType::Directional:
        return sampleDirectionalLight(shadingPosW, light, ls);
    case LightType::Rect:
        return sampleRectAreaLight(shadingPosW, light, sampleNext2D(sg), ls);
    default:
        ls = {};

        return false;
    }
}

#endif
