#ifndef RENDERLAB_CAMERA_RAY_SLANG
#define RENDERLAB_CAMERA_RAY_SLANG

#include "math/constant.slang"
#include "Scene/Scene.slang"

RayDesc createCameraRay(uint2 pixel, uint2 frameDim, float2 pixelSample)
{
    float2 uv = (float2(pixel) + pixelSample) / float2(frameDim);
    SceneUniforms camera = scene.camera;
    float4 farPoint = mul(camera.inverseViewProjection, float4(uvToClip(uv), 1.0, 1.0));

    RayDesc ray;
    ray.Origin = camera.cameraPosition.xyz;
    ray.Direction = normalize(farPoint.xyz / farPoint.w - ray.Origin);
    ray.TMin = kRayEpsilon;
    ray.TMax = kMaxRayDistance;
    return ray;
}

#endif
