#ifndef RENDERLAB_CAMERA_RAY_SLANG
#define RENDERLAB_CAMERA_RAY_SLANG

#include "math/constant.slang"
#include "scene/scene_data.slang"

RayDesc createCameraRay(uint2 pixel, float2 pixelSample)
{
    const ViewportRect viewport = scene.globals[0].viewport;
    float2 viewportUv = (float2(pixel) + pixelSample - viewport.offset) / viewport.extent;
    SceneUniforms camera = scene.globals[0].camera;
    float4 farPoint = mul(camera.inverseViewProjection, float4(uvToClip(viewportUv), 1.0, 1.0));

    RayDesc ray;
    ray.Origin = camera.cameraPosition.xyz;
    ray.Direction = normalize(farPoint.xyz / farPoint.w - ray.Origin);
    ray.TMin = kRayEpsilon;
    ray.TMax = kMaxRayDistance;
    return ray;
}

#endif
