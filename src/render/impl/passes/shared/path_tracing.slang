#ifndef RENDERLAB_PATH_TRACING_SLANG
#define RENDERLAB_PATH_TRACING_SLANG

#include "Scene/Raytracing.slang"
#include "Scene/Shading.slang"
#include "Utils/Math/MathConstants.slangh"
#include "Utils/Sampling/SampleGenerator.slang"

static const uint  kRussianRouletteStartBounce = 3;
static const float kShadowRayEpsilon           = 0.001f;

struct ShadowRayData
{
    uint visible;
};

struct ScatterRayPayload
{
    PackedHitInfo packedHit;
};

ShadingData loadShadingData(const HitInfo hit, const float3 rayOrigin, const float3 rayDir)
{
    VertexData v = {};
    uint materialID = {};

    if (hit.getType() == HitType::Triangle)
    {
        const TriangleHit triangleHit = hit.getTriangleHit();
        v = gScene.getVertexData(triangleHit);
        materialID = gScene.getMaterialID(triangleHit.instanceID);
    }

    return gScene.materials.prepareShadingData(v, materialID, -rayDir);
}

bool traceShadowRay(const float3 origin, const float3 dir, const float distance)
{
    if (distance <= kShadowRayEpsilon)
        return true;

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = 0.f;
    ray.TMax      = distance < FLT_MAX ? distance - kShadowRayEpsilon : FLT_MAX;

    static const uint kRayFlags =
        RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    ShadowRayData payload = { 0 };
    traceSceneRay(ray, RayType::Visibility, kRayFlags, payload);

    return payload.visible != 0;
}

HitInfo traceScatterRay(const RayDesc ray)
{
    ScatterRayPayload payload = { makeInvalidHit().pack() };
    traceSceneRay(ray, RayType::Scatter, RAY_FLAG_FORCE_NON_OPAQUE, payload);

    return HitInfo(payload.packedHit);
}

SampleGenerator createPathSampleGenerator(uint2 pixel, uint width, uint sampleSeed)
{
    uint pixelIndex = pixel.x + pixel.y * width;

    return SampleGenerator(pixelIndex, sampleSeed);
}

[shader("miss")]
void missMain(inout ScatterRayPayload payload)
{
    payload.packedHit = makeInvalidHit().pack();
}

[shader("miss")]
void visibilityMissMain(inout ShadowRayData payload)
{
    payload.visible = 1;
}

[shader("closesthit")]
void closestHitMain(inout ScatterRayPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    payload.packedHit = HitInfo(triangleHit).pack();
}

#endif
