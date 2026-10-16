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

#endif
