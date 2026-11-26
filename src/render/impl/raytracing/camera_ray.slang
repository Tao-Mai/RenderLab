#ifndef RENDERLAB_CAMERA_RAY_SLANG
#define RENDERLAB_CAMERA_RAY_SLANG

#include "math/constant.slang"
#include "Scene/Scene.slang"

RayDesc createCameraRay(uint2 pixel, uint2 frameDim, float2 pixelSample)
{
    float2 uv = (float2(pixel) + pixelSample) / float2(frameDim);
    SceneUniforms camera = scene.camera;
    float2 ndc = uvToClip(uv);

    RayDesc ray;
    ray.Origin = camera.cameraPosition.xyz;
    ray.Direction = normalize(ndc.x * camera.cameraU + ndc.y * camera.cameraV + camera.cameraW);
    ray.TMin = kRayEpsilon;
    ray.TMax = kMaxRayDistance;

    return ray;
}

#endif
