#define SCENE_RAY_TRACING 1
#include "raytracing/alpha_test.slang"
#include "raytracing/camera_ray.slang"
#include "raytracing/visibility_buffer.slang"

[format("rgba32ui")] RWTexture2D<uint4> vbuffer;

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    VBufferHit hit = {kInvalidVBufferHit};
    if (scene.globals[0].viewport.containsPixel(pixel))
    {
        RayDesc ray = createCameraRay(pixel, float2(0.5));
        TraceRay(scene.rtAccel, RAY_FLAG_FORCE_NON_OPAQUE, kSceneMask, 0, 0, 0, ray, hit);
    }
    vbuffer[pixel] = hit.packed;
}

[shader("miss")]
void missMain(inout VBufferHit hit)
{
    hit.packed = kInvalidVBufferHit;
}

[shader("closesthit")]
void closestHitMain(inout VBufferHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    hit.packed = uint4(InstanceID(), PrimitiveIndex(), asuint(attributes.barycentrics));
}

[shader("anyhit")]
void anyHitMain(inout VBufferHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    if (isMaskedTriangle(InstanceID(), PrimitiveIndex(), attributes.barycentrics))
        IgnoreHit();
}
