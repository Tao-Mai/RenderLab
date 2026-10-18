#ifndef RENDERLAB_VIEW_DATA_SLANG
#define RENDERLAB_VIEW_DATA_SLANG

// Must match ViewUniforms in ShaderData.h; column-major storage.
struct ViewUniforms
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4x4 view;
};

float3 reconstructWorldPosition(float2 ndc, float depth, float4x4 inverseViewProjection)
{
    float4 world = mul(inverseViewProjection, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

float linearViewDepth(float3 position, float4x4 view)
{
    return -mul(view, float4(position, 1.0)).z;
}

float3 reconstructWorldPositionFromLinearDepth(float2 ndc, float depth, ViewUniforms view)
{
    float3 nearPosition = reconstructWorldPosition(ndc, 0.0, view.inverseViewProjection);
    float3 farPosition = reconstructWorldPosition(ndc, 1.0, view.inverseViewProjection);
    float nearDepth = linearViewDepth(nearPosition, view.view);
    float farDepth = linearViewDepth(farPosition, view.view);
    return lerp(nearPosition, farPosition, (depth - nearDepth) / (farDepth - nearDepth));
}

#endif
