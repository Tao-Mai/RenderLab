#include "path_tracing.slang"

[[vk::binding(1, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(7, 3)]] [[vk::image_format("r32f")]] RWTexture2D<float> depthImage;
[[vk::binding(8, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> normalImage;

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;
    uint state = (pixel.x + pixel.y * DispatchRaysDimensions().x) ^ (parameters.sampleIndex * kSampleSeedMultiplier);
    PathResult result = {float3(0.0), -1.0, float3(0.0)};
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = parameters.createRay(pixel, float2(0.5));
        result = tracePath(ray, kSceneMask, RAY_FLAG_FORCE_OPAQUE, state);
    }
    outputImage[pixel] = float4(result.radiance, 1.0);
    depthImage[pixel] = result.primaryDepth;
    normalImage[pixel] = float4(result.primaryNormal, 0.0);
}
