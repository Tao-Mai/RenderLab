#ifndef RENDERLAB_REALTIME_SCENE_DATA_SLANG
#define RENDERLAB_REALTIME_SCENE_DATA_SLANG

#include "raytracing/geometry.slang"

static const uint kInvalidEmissiveTriangle = 0xffffffffu;
static const uint kEmissiveTriangleHeaderSize = 16;
static const uint kEmissiveTriangleStride = 80;

struct RayTracingHitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint firstIndex;
    uint materialIndex;
    uint firstEmissiveTriangle;
    uint padding;

    VertexAttributes loadVertex(uint index)
    {
        return loadVertexAttributes(vertexAddress, index);
    }
};

struct EmissiveTriangleTask
{
    float4x4 model;
    uint objectIndex;
    uint firstTriangle;
    uint triangleCount;
    uint padding;
};

struct EmissiveTriangle
{
    float3 position;
    uint materialIndex;
    float3 edge1;
    float area;
    float3 edge2;
    uint padding;
    float2 uv0;
    float2 uv1;
    float2 uv2;
    uint2 uvPadding;
};

#endif
