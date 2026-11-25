#include "BdptBindings.h"

ConstantBuffer<BdptBinding::ResolveParameters> parameters;
StructuredBuffer<BdptBinding::AccumulatedPixel> cameraHistory;
StructuredBuffer<uint4> splats;
[format("rgba16f")] RWTexture2D<float4> outputColor;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 size;
    outputColor.GetDimensions(size.x, size.y);
    uint2 pixel = dispatchId.xy;
    if (any(pixel >= size))
        return;

    float3 color = float3(0.0);
    uint index = pixel.x + pixel.y * size.x;
    BdptBinding::AccumulatedPixel history = cameraHistory[index];
    if (history.sampleCount > 0)
        color = history.radiance / float(history.sampleCount);
    if (parameters.lightSplatScale > 0.0)
        color += asfloat(splats[index].rgb) * parameters.lightSplatScale;
    outputColor[pixel] = float4(color, 1.0);
}
