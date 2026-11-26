#include "raytracing/alpha_test.slang"
#include "raytracing/camera_ray.slang"
#include "raytracing/visibility_buffer.slang"

[format("rgba32ui")] RWTexture2D<uint4> hitImage;

[shader("raygeneration")]
void raygenMain()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const RayDesc ray = createCameraRay(pixel, DispatchRaysDimensions().xy, float2(0.5));
    VBufferHit hit = {kVBufferMiss};
    traceSceneRay(ray, RayType::Scatter, RAY_FLAG_FORCE_NON_OPAQUE, hit);

    hitImage[pixel] = hit.packed;
}

[shader("closesthit")]
void closestHitMain(inout VBufferHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    hit.packed = uint4(InstanceID(), PrimitiveIndex(), asuint(attributes.barycentrics));
}

[shader("anyhit")]
void scatterAnyHitMain(inout VBufferHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    if (isMaskedTriangle(InstanceID(), PrimitiveIndex(), attributes.barycentrics))
        IgnoreHit();
}

[shader("miss")]
void missMain(inout VBufferHit hit) { hit.packed = kVBufferMiss; }
