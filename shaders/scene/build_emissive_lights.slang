#include "scene/emissive_light_data.slang"
#include "raytracing/geometry.slang"
#include "math/constant.slang"

struct EmissiveMesh
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    float4x4 model;
    uint firstIndex;
    uint materialIndex;
    uint triangleOffset;
    uint flipNormal;
    float emissionWeight;
    uint padding0, padding1, padding2;
};

struct BuildParameters
{
    uint meshCount;
    uint triangleCount;
    uint padding0, padding1;
};

ConstantBuffer<BuildParameters> parameters;
StructuredBuffer<EmissiveMesh> meshes;
RWStructuredBuffer<EmissiveTriangle> triangles;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadIndex : SV_DispatchThreadID)
{
    uint triangleIndex = threadIndex.x + threadIndex.y * kLightDispatchRowWidth;
    if (triangleIndex >= parameters.triangleCount)
        return;

    uint first = 0;
    uint end = parameters.meshCount;
    while (first + 1 < end)
    {
        uint middle = (first + end) / 2;
        if (meshes[middle].triangleOffset <= triangleIndex)
            first = middle;
        else
            end = middle;
    }

    EmissiveMesh mesh = meshes[first];
    uint* indices = (uint*)mesh.indexAddress + mesh.firstIndex + 3 * (triangleIndex - mesh.triangleOffset);
    VertexAttributes v0 = loadVertexAttributes(mesh.vertexAddress, indices[0]);
    VertexAttributes v1 = loadVertexAttributes(mesh.vertexAddress, indices[1]);
    VertexAttributes v2 = loadVertexAttributes(mesh.vertexAddress, indices[2]);
    float3 p0 = mul(mesh.model, float4(v0.position, 1.0)).xyz;
    float3 p1 = mul(mesh.model, float4(v1.position, 1.0)).xyz;
    float3 p2 = mul(mesh.model, float4(v2.position, 1.0)).xyz;
    float3 crossProduct = cross(p1 - p0, p2 - p0);
    float doubleArea = length(crossProduct);
    float area = 0.5 * doubleArea;
    float3 normal = doubleArea > 0.0 ? crossProduct / doubleArea : float3(0.0);
    if (mesh.flipNormal != 0)
        normal = -normal;

    EmissiveTriangle triangle;
    triangle.p0Area = float4(p0, area);
    triangle.p1 = float4(p1, 0.0);
    triangle.p2 = float4(p2, 0.0);
    triangle.normal = float4(normal, 0.0);
    triangle.uv01 = float4(v0.uv, v1.uv);
    triangle.uv2 = v2.uv;
    triangle.materialIndex = mesh.materialIndex;
    triangle.power = area * mesh.emissionWeight * PI;
    triangles[triangleIndex] = triangle;
}
