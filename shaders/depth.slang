#include "mesh_vertex.slang"
#include "material_data.slang"

struct DepthVertexOutput
{
    precise float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
DepthVertexOutput vertMain(VertexInput input)
{
    DepthVertexOutput output;
    float4 worldPosition;
    output.position = transformMeshPosition(input.position, worldPosition);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
void fragMain(DepthVertexOutput input)
{
    if (material.alphaMode == 1)
    {
        float alpha = baseColorTexture.Sample(baseColorSampler, input.uv).a * material.baseColorFactor.a;
        applyAlphaTest(alpha);
    }
}
