#ifndef RENDERLAB_CAMERA_DATA_SLANG
#define RENDERLAB_CAMERA_DATA_SLANG

// Must match CameraData in ShaderData.h; column-major storage.
struct CameraData
{
    float4x4 viewMat;      // Camera view matrix.
    float4x4 viewProjMat;  // Camera view-projection matrix.

    float3   posW;         // Camera world-space position.
    float    _padding0;
    float3   cameraU;      // Camera right image plane vector (scaled by horizontal half-FOV).
    float    _padding1;
    float3   cameraV;      // Camera up image plane vector (scaled by vertical half-FOV).
    float    _padding2;
    float3   cameraW;      // Camera forward direction.
    float    _padding3;
};

#endif
