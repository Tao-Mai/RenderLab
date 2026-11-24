#ifndef RENDERLAB_CAMERA_RAY_SLANG
#define RENDERLAB_CAMERA_RAY_SLANG

#include "scene/scene_data.slang"

RayDesc createCameraRay(float2 viewportUv)
{
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
