#ifndef RENDERLAB_EMISSIVE_LIGHT_SAMPLER_HELPERS_SLANG
#define RENDERLAB_EMISSIVE_LIGHT_SAMPLER_HELPERS_SLANG

#include "Rendering/Lights/EmissiveLightSamplerInterface.slang"
#include "Scene/Scene.slang"
#include "Utils/Math/MathConstants.slangh"

bool sampleTriangle(const float3 posW, const uint triangleIndex, const float2 u, out TriangleLightSample ls)
{
    ls = {};
    ls.triangleIndex = triangleIndex;
    const EmissiveTriangle tri = gScene.lightCollection.getTriangle(triangleIndex);
    if (tri.area <= 0.f)
        return false;

    const float su = sqrt(u.x);
    const float3 barycentrics = float3(1.f - su, su * (1.f - u.y), su * u.y);
    ls.posW = tri.getPosition(barycentrics);

    float3 toLight = ls.posW - posW;
    const float distSqr = max(FLT_MIN, dot(toLight, toLight));
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;
    ls.normalW = tri.normal;

    float cosTheta = dot(ls.normalW, -ls.dir);
    if (cosTheta <= 0.f)
        return false;

    const float2 uv = tri.getTexCoord(barycentrics);
    if (gScene.materials.alphaTest(uv, tri.materialID))
        return false;

    ls.Le = gScene.materials.evalEmissive(tri.materialID, uv);

    const float denom = max(FLT_MIN, cosTheta * tri.area);
    ls.pdf = distSqr / denom;

    return true;
}

float evalTrianglePdf(const float3 posW, const TriangleLightHit hit)
{
    const float area = gScene.lightCollection.getTriangle(hit.triangleIndex).area;
    if (area <= 0.f)
        return 0.f;

    float3 toLight = hit.posW - posW;
    const float distSqr = dot(toLight, toLight);
    if (distSqr <= FLT_MIN)
        return 0.f;

    float3 L = toLight / sqrt(distSqr);
    float cosTheta = dot(hit.normalW, -L);
    if (cosTheta <= 0.f)
        return 0.f;

    const float denom = max(FLT_MIN, cosTheta * area);

    return distSqr / denom;
}

#endif
