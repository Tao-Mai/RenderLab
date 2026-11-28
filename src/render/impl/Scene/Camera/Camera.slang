#ifndef RENDERLAB_CAMERA_SLANG
#define RENDERLAB_CAMERA_SLANG

#include "Scene/Camera/CameraData.slang"
#include "Utils/Math/MathConstants.slangh"

struct Camera
{
    CameraData data;

    float3 getPosition() { return data.posW; }
    float4x4 getViewProj() { return data.viewProjMat; }

    float3 computeNonNormalizedRayDirPinhole(uint2 pixel, uint2 frameDim, float2 subpixel = float2(0.5))
    {
        float2 p = (float2(pixel) + subpixel) / float2(frameDim);
        float2 ndc = float2(2.f, -2.f) * p + float2(-1.f, 1.f);

        return ndc.x * data.cameraU + ndc.y * data.cameraV + data.cameraW;
    }

    RayDesc computeRayPinhole(uint2 pixel, uint2 frameDim, float2 subpixel = float2(0.5))
    {
        RayDesc ray;
        ray.Origin = data.posW;
        ray.Direction = normalize(computeNonNormalizedRayDirPinhole(pixel, frameDim, subpixel));
        ray.TMin = 0.001f;
        ray.TMax = FLT_MAX;

        return ray;
    }
};

#endif
