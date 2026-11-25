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

struct TriangleVertices
{
    VertexAttributes v0, v1, v2;

    VertexAttributes interpolate(float2 barycentrics)
    {
        float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics);
        return {
            v0.position * weights.x + v1.position * weights.y + v2.position * weights.z,
            v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z,
            v0.uv * weights.x + v1.uv * weights.y + v2.uv * weights.z,
            v0.tangent * weights.x + v1.tangent * weights.y + v2.tangent * weights.z,
        };
    }
};

TriangleVertices loadTriangleVertices(uint64_t vertexAddress, uint64_t indexAddress, uint firstIndex)
{
    uint* indices = (uint*)indexAddress + firstIndex;
    return {
        loadVertexAttributes(vertexAddress, indices[0]),
        loadVertexAttributes(vertexAddress, indices[1]),
        loadVertexAttributes(vertexAddress, indices[2]),
    };
}

#endif
