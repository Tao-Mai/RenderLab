#include "mesh_surface.slang"
#include "scene_lighting.slang"

[shader("fragment")]
float4 fragMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    Surface surface = sampleSurface(input, isFrontFace);
    return float4(evaluateLighting(surface, uint2(input.position.xy)), surface.baseColor.a);
}
