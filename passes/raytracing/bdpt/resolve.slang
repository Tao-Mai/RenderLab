#include "bdpt_parameters.slang"
#include "raytracing/accumulation/path_resolve.slang"

StructuredBuffer<uint4> splats;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchId : SV_DispatchThreadID)
{
    PathParameters path = parameters.path;
    uint2 pixel = dispatchId.xy;
    if (any(pixel >= path.imageSize))
        return;

    uint index = pixel.x + pixel.y * path.imageSize.x;
    float3 color = path.containsPixel(pixel) ? averageCameraSamples(index) : float3(0.0);
    if (path.containsPixel(pixel) && parameters.lightSamples > 0.0)
        color += asfloat(splats[index].xyz) / parameters.lightSamples;
    outputImage[pixel] = float4(color, 1.0);
}
