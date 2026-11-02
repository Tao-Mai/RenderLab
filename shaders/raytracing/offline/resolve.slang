#include "raytracing/offline/path_parameters.slang"

[[vk::binding(OfflineRayTracingBinding::kAccumulation, 3)]] StructuredBuffer<AccumulatedPixel> accumulation;
[[vk::binding(OfflineRayTracingBinding::kColor, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
#if BDPT
[[vk::binding(OfflineRayTracingBinding::kSplats, 3)]] StructuredBuffer<uint4> splats;
#endif

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchId : SV_DispatchThreadID)
{
    uint width, height;
    outputImage.GetDimensions(width, height);
    uint2 pixel = dispatchId.xy;
    if (pixel.x >= width || pixel.y >= height)
    {
        return;
    }

    float2 localPixel = float2(pixel) + 0.5 - parameters.viewportOffset;
    if (any(localPixel < 0.0) || any(localPixel >= parameters.viewportSize))
    {
        outputImage[pixel] = float4(0.0, 0.0, 0.0, 1.0);

        return;
    }

    uint index = pixel.x + pixel.y * width;
    AccumulatedPixel history = accumulation[index];
    float3 color = history.sampleCount > 0 ? history.color / float(history.sampleCount) : float3(0.0);
#if BDPT
    if (parameters.lightSamples > 0.0)
    {
        color += asfloat(splats[index].xyz) / parameters.lightSamples;
    }
#endif
    outputImage[pixel] = float4(color, 1.0);
}
