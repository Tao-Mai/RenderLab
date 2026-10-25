#include "path_tracing.slang"

[[vk::binding(1, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(7, 3)]] [[vk::image_format("r32f")]] RWTexture2D<float> depthImage;
[[vk::binding(8, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> normalImage;
#if SVGF
[[vk::binding(9, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> indirectImage;
[[vk::binding(10, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> emissionImage;
[[vk::binding(11, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> albedoImage;
#endif

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;
    uint state = (pixel.x + pixel.y * DispatchRaysDimensions().x) ^ (parameters.sampleIndex * kSampleSeedMultiplier);
    PathResult result = {};
    result.primaryDepth = -1.0;
    result.primaryAlbedo = float3(1.0);
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = parameters.createRay(pixel, float2(0.5));
        result = tracePath(ray, kSceneMask | kLightMask, RAY_FLAG_FORCE_OPAQUE, state);
    }
#if SVGF
    outputImage[pixel] = float4(result.directRadiance, 1.0);
    indirectImage[pixel] = float4(result.indirectRadiance, 1.0);
    emissionImage[pixel] = float4(result.emission, 1.0);
    albedoImage[pixel] = float4(result.primaryAlbedo, 1.0);
#else
    outputImage[pixel] = float4(result.directRadiance + result.indirectRadiance + result.emission, 1.0);
#endif
    depthImage[pixel] = result.primaryDepth;
    normalImage[pixel] = float4(result.primaryNormal, 0.0);
}
