#define SCENE_RAY_TRACING 1
#include "raytracing/alpha_test.slang"
#include "raytracing/camera_ray.slang"
#include "raytracing/visibility_buffer.slang"

struct VBufferParameters
{
    float2 viewportOffset;
    float2 viewportExtent;
};

ConstantBuffer<VBufferParameters> parameters;
[format("rgba32ui")] RWTexture2D<uint4> vbuffer;

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    float2 local = float2(pixel) + 0.5 - parameters.viewportOffset;
    VBufferHit hit = {kInvalidVBufferHit};
    if (all(local >= 0.0) && all(local < parameters.viewportExtent))
    {
        RayDesc ray = createCameraRay(local / parameters.viewportExtent);
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
