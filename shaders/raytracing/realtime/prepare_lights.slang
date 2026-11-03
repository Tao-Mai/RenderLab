#include "raytracing/realtime/geometry.slang"

[[vk::binding(0, 0)]] StructuredBuffer<RayTracingHitData> objects;
[[vk::binding(1, 0)]] StructuredBuffer<EmissiveTriangleTask> tasks;
[[vk::binding(2, 0)]] RWByteAddressBuffer triangles;

struct PrepareParameters
{
    uint triangleCount;
    uint taskCount;
};
[[vk::push_constant]] ConstantBuffer<PrepareParameters> parameters;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint triangleIndex = thread.x;
    if (triangleIndex >= parameters.triangleCount)
    {
        return;
    }

    // Tasks occupy contiguous triangle ranges, one range per emissive submesh instance.
    uint begin = 0;
    uint end = parameters.taskCount;
    while (begin + 1 < end)
    {
        uint middle = (begin + end) / 2;
        if (tasks[middle].firstTriangle <= triangleIndex)
        {
            begin = middle;
        }
        else
        {
            end = middle;
        }
    }
    EmissiveTriangleTask task = tasks[begin];
    RayTracingHitData object = objects[task.objectIndex];
    uint* indices = (uint*)object.indexAddress;
    uint firstIndex = object.firstIndex + (triangleIndex - task.firstTriangle) * 3;
    VertexAttributes v0 = object.loadVertex(indices[firstIndex]);
    VertexAttributes v1 = object.loadVertex(indices[firstIndex + 1]);
    VertexAttributes v2 = object.loadVertex(indices[firstIndex + 2]);

    EmissiveTriangle light = {};
    light.position = mul(task.model, float4(v0.position, 1.0)).xyz;
    light.edge1 = mul((float3x3)task.model, v1.position - v0.position);
    light.edge2 = mul((float3x3)task.model, v2.position - v0.position);
    light.area = 0.5 * length(cross(light.edge1, light.edge2));
    light.materialIndex = object.materialIndex;
    light.uv0 = v0.uv;
    light.uv1 = v1.uv;
    light.uv2 = v2.uv;
    triangles.Store<EmissiveTriangle>(kEmissiveTriangleHeaderSize + triangleIndex * kEmissiveTriangleStride, light);
}
