#ifndef RENDERLAB_SHADING_UTILS_SLANG
#define RENDERLAB_SHADING_UTILS_SLANG

#include "Rendering/Materials/IsotropicGGX.slang"
#include "Scene/ShadingData.slang"
#include "Utils/Color/ColorHelpers.slang"

static const uint kNormalEncodingRgb     = 0;
static const uint kNormalEncodingRgSnorm = 2;

float getMetallic(float3 diffuse, float3 spec)
{
    float d = luminance(diffuse);
    float s = luminance(spec);
    if (s == 0.f)
        return 0.f;

    float b = s + d - 0.08f;
    float c = 0.04f - s;

    return (sqrt(b * b - 0.16f * c) - b) * 12.5f;
}

ShadingFrame computeShadingFrameFromNormalMap(
    const ShadingData sd, const float3 textureNormal, const float normalScale, const uint normalEncoding)
{
    float3 mapN = textureNormal * 2.f - 1.f;
    if (normalEncoding != kNormalEncodingRgb)
    {
        float2 xy = normalEncoding == kNormalEncodingRgSnorm ? textureNormal.xy : mapN.xy;
        mapN = float3(xy, sqrt(saturate(1.f - dot(xy, xy))));
    }
    mapN.xy *= normalScale;

    bool valid;

    return ShadingFrame::createSafe(sd.frame.fromLocal(mapN), sd.tangentW, valid);
}

void flipShadingNormal(const ShadingData sd, const bool doubleSided, inout ShadingFrame sf)
{
    if (!sd.frontFacing && doubleSided)
        sf.N = -sf.N;
}

void adjustShadingNormal(const ShadingData sd, inout ShadingFrame sf)
{
    float3 Ng = sd.getOrientedFaceNormal();
    float signN = dot(sf.N, Ng) >= 0.f ? 1.f : -1.f;
    float3 Ns = signN * sf.N;

    const float kCosThetaThreshold = 0.1f;
    float cosTheta = dot(sd.V, Ns);
    if (cosTheta <= kCosThetaThreshold)
    {
        float t = saturate(cosTheta * (1.f / kCosThetaThreshold));
        bool valid;
        sf = ShadingFrame::createSafe(signN * lerp(Ng, Ns, t), sd.tangentW, valid);
    }
}

bool isValidHemisphereReflectionOrTransmission(
    const ShadingData sd,
    const ShadingFrame sf,
    const float3 wiLocal,
    const float3 woLocal,
    const float3 wo)
{
    const bool isTransmission = wiLocal.z * woLocal.z < 0.f;

    if (min(wiLocal.z, abs(woLocal.z)) < kMinCosTheta)
        return false;

    bool wiTop = sd.frontFacing;
    bool woTop = dot(wo, sd.faceN) >= 0.f;

    if (isTransmission)
    {
        if (wiTop == woTop)
            return false;
    }
    else
    {
        if (wiTop != woTop)
            return false;
    }

    bool shadingTop = dot(sf.N, sd.faceN) >= 0.f;
    if (wiTop != shadingTop)
        return false;

    return true;
}

#endif
