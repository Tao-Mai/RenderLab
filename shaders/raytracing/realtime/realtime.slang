#include "raytracing/realtime/path_tracing.slang"

[[vk::binding(RealtimeRayTracingBinding::kColor, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(RealtimeRayTracingBinding::kDepth, 3)]] [[vk::image_format("r32f")]] RWTexture2D<float> depthImage;
[[vk::binding(RealtimeRayTracingBinding::kNormal, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> normalImage;
#if SVGF
[[vk::binding(RealtimeRayTracingBinding::kIndirect, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> indirectImage;
[[vk::binding(RealtimeRayTracingBinding::kEmission, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> emissionImage;
[[vk::binding(RealtimeRayTracingBinding::kAlbedo, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> albedoImage;
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
    RestirSurfaceData primarySurface = {};
    RestirReservoir primaryReservoir = {};
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = parameters.createRay(pixel, float2(0.5));
        result = tracePath(ray, kSceneMask | kLightMask, RAY_FLAG_FORCE_OPAQUE, state, primarySurface, primaryReservoir);
    }
    uint index = pixel.y * restirFrame.width + pixel.x;
    restirSurfaces[index] = primarySurface;
    restirInitialReservoirs[index] = primaryReservoir;
#if SVGF
    outputImage[pixel] = float4(0.0, 0.0, 0.0, 1.0);
    indirectImage[pixel] = float4(result.indirectRadiance, 1.0);
    emissionImage[pixel] = float4(result.emission, 1.0);
    albedoImage[pixel] = float4(result.primaryAlbedo, 1.0);
#else
    outputImage[pixel] = float4(result.indirectRadiance + result.emission, 1.0);
#endif
    depthImage[pixel] = result.primaryDepth;
    normalImage[pixel] = float4(result.primaryNormal, 0.0);
}

#include "raytracing/realtime/restir_reuse.slang"
