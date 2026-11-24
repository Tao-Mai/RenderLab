#ifndef RENDERLAB_MESH_VERTEX_SLANG
#define RENDERLAB_MESH_VERTEX_SLANG

#include "scene/scene_data.slang"

struct VertexInput
{
    float3 position : POSITION;

    float3 normal : NORMAL;

    float2 uv : TEXCOORD;

    float4 tangent : TANGENT;

    property float3 tangentDirection { get { return tangent.xyz; } }
    property float tangentHandedness { get { return tangent.w; } }
};

struct PushConstants
{
    float4x4 model;
    uint materialIndex;
    uint padding0;
    uint padding1;
    uint padding2;
};

ConstantBuffer<PushConstants> pushConstants;

// Both passes use the same precise position calculation for the Equal depth test.
float4 transformMeshPosition(float3 position, out float4 worldPosition)
{
    precise float4 world = mul(pushConstants.model, float4(position, 1.0));
    precise float4 clip = mul(sceneUniforms.viewProjection, world);
    worldPosition = world;
    return clip;
}

#endif
