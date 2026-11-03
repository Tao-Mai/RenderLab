#ifndef RENDERLAB_REALTIME_GEOMETRY_SLANG
#define RENDERLAB_REALTIME_GEOMETRY_SLANG

static const uint kInvalidEmissiveTriangle = 0xffffffffu;
static const uint kVertexStrideFloats = 12;
static const uint kEmissiveTriangleHeaderSize = 16;
static const uint kEmissiveTriangleStride = 80;

struct VertexAttributes
{
    float3 position;
    float3 normal;
    float2 uv;
    float4 tangent;
};

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
        // Match the packed 48-byte C++ Vertex rather than std430 float3 alignment.
        float* vertex = (float*)vertexAddress + index * kVertexStrideFloats;
        return {
            float3(vertex[0], vertex[1], vertex[2]),
            float3(vertex[3], vertex[4], vertex[5]),
            float2(vertex[6], vertex[7]),
            float4(vertex[8], vertex[9], vertex[10], vertex[11]),
        };
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
