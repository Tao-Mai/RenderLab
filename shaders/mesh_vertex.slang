#ifndef RENDERLAB_MESH_VERTEX_SLANG
#define RENDERLAB_MESH_VERTEX_SLANG

#include "scene_data.slang"

struct VertexInput
{
    [[vk::location(0)]]
    float3 position : POSITION;

    [[vk::location(1)]]
    float3 normal : NORMAL;

    [[vk::location(2)]]
    float2 uv : TEXCOORD;

    [[vk::location(3)]]
    float4 tangent : TANGENT;
};

struct PushConstants
{
    float4x4 model;
    uint materialIndex;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::push_constant]]
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
