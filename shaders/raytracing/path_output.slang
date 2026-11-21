#ifndef RENDERLAB_REALTIME_OUTPUT_SLANG
#define RENDERLAB_REALTIME_OUTPUT_SLANG

#include "path_tracing.slang"

[[vk::binding(3, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(4, 3)]] [[vk::image_format("r32f")]] RWTexture2D<float> depthImage;
[[vk::binding(5, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> normalImage;
#if SVGF
[[vk::binding(6, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> indirectImage;
[[vk::binding(7, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> emissionImage;
[[vk::binding(8, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> albedoImage;
#endif

void writePathResult(uint2 pixel, PathResult result)
{
#if SVGF
    outputImage[pixel]   = float4(result.directRadiance, 1.0);
    indirectImage[pixel] = float4(result.indirectRadiance, 1.0);
    emissionImage[pixel] = float4(result.emission, 1.0);
    albedoImage[pixel]   = float4(result.primaryAlbedo, 1.0);
#else
    outputImage[pixel] = float4(result.directRadiance + result.indirectRadiance + result.emission, 1.0);
#endif
    depthImage[pixel]  = result.primaryDepth;
    normalImage[pixel] = float4(result.primaryNormal, 0.0);
}

#endif
