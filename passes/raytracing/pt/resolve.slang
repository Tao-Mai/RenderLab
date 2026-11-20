#include "pt_parameters.slang"
#include "raytracing/offline/path_resolve.slang"

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
    outputImage[pixel] = float4(color, 1.0);
}
