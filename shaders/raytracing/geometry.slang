#ifndef RENDERLAB_RAY_TRACING_GEOMETRY_SLANG
#define RENDERLAB_RAY_TRACING_GEOMETRY_SLANG

static const uint kVertexStrideFloats = 12;

struct VertexAttributes
{
    float3 position;
    float3 normal;
    float2 uv;
    float4 tangent;
};

VertexAttributes loadVertexAttributes(uint64_t vertexAddress, uint index)
{
    // Match the packed 48-byte C++ Vertex.
    float* vertex = (float*)vertexAddress + index * kVertexStrideFloats;
    return {
        float3(vertex[0], vertex[1], vertex[2]),
        float3(vertex[3], vertex[4], vertex[5]),
        float2(vertex[6], vertex[7]),
        float4(vertex[8], vertex[9], vertex[10], vertex[11]),
    };
}

#endif
