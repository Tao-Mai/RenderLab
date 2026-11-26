#ifndef RENDERLAB_CAMERA_DATA_SLANG
#define RENDERLAB_CAMERA_DATA_SLANG

// Must match SceneUniforms in ShaderData.h; column-major storage.
struct SceneUniforms
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float3 cameraU;
    float padding0;
    float3 cameraV;
    float padding1;
    float3 cameraW;
    float padding2;
    float4x4 view;
};

#endif
