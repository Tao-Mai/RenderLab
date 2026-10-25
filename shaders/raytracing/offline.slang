#include "path_tracing.slang"
#include "low_discrepancy.slang"

[[vk::binding(1, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(7, 3)]] RWStructuredBuffer<float4> accumulation;

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint pixelIndex = pixel.x + pixel.y * DispatchRaysDimensions().x;
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;
    uint state = pixelIndex ^ (parameters.sampleIndex * kSampleSeedMultiplier);
    randomUint(state);
    float2 pixelSample = sampleSobol2D(parameters.sampleIndex, pixelIndex);
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = parameters.createRay(pixel, pixelSample);
        PathResult result = tracePath(ray, kSceneMask | kLightMask,
            RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES, state);
        float3 sum = result.directRadiance + result.indirectRadiance + result.emission;
        if (parameters.sampleIndex != 0)
            sum += accumulation[pixelIndex].rgb;
        accumulation[pixelIndex] = float4(sum, 0.0);
        outputImage[pixel] = float4(sum / (float(parameters.sampleIndex) + 1.0), 1.0);
    }
    else
    {
        accumulation[pixelIndex] = float4(0.0);
        outputImage[pixel] = float4(0.0, 0.0, 0.0, 1.0);
    }
}
