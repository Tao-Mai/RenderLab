#include "Scene/Raytracing.slang"

[format("rgba32ui")] RWTexture2D<PackedHitInfo> hitImage;

struct VBufferPayload
{
    PackedHitInfo packedHit;
};

[shader("raygeneration")]
void raygenMain()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const RayDesc ray = gScene.camera.computeRayPinhole(pixel, DispatchRaysDimensions().xy);
    VBufferPayload payload = { makeInvalidHit().pack() };
    traceSceneRay(ray, RayType::Scatter, RAY_FLAG_FORCE_NON_OPAQUE, payload);

    hitImage[pixel] = payload.packedHit;
}

[shader("closesthit")]
void closestHitMain(inout VBufferPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attributes.barycentrics;
    payload.packedHit = HitInfo(triangleHit).pack();
}

[shader("anyhit")]
void scatterAnyHitMain(inout VBufferPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (alphaTest(getGeometryInstanceID(), PrimitiveIndex(), attributes))
        IgnoreHit();
}

[shader("miss")]
void missMain(inout VBufferPayload payload)
{
    payload.packedHit = makeInvalidHit().pack();
}
