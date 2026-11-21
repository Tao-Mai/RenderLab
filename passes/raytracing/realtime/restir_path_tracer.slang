#include "raytracing/path_tracing.slang"
#include "render/denoise/DenoiserInputs.h"
#include "restir.slang"

#if SVGF
[[vk::binding(3, 3)]] [[vk::image_format("rgba16f")]] RWTexture2DArray<float4> outputImage;
[[vk::binding(7, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> emissionImage;
[[vk::binding(8, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> albedoImage;
#else
[[vk::binding(3, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
#endif
[[vk::binding(4, 3)]] [[vk::image_format("r32f")]] RWTexture2D<float> depthImage;
[[vk::binding(5, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> normalImage;

struct PathResult
{
    float3 directRadiance;
    float3 indirectRadiance;
    float3 emission;
    float  primaryDepth;
    float3 primaryNormal;
    float3 primaryAlbedo;
};

PathResult createPathResult()
{
    PathResult result    = {};
    result.primaryDepth  = -1.0;
    result.primaryAlbedo = float3(1.0);
    return result;
}

void writePathResult(uint2 pixel, PathResult result)
{
#if SVGF
    outputImage[uint3(pixel, kDirectLightingLayer)] = float4(result.directRadiance, 1.0);
    outputImage[uint3(pixel, kIndirectLightingLayer)] = float4(result.indirectRadiance, 1.0);
    emissionImage[pixel] = float4(result.emission, 1.0);
    albedoImage[pixel]   = float4(result.primaryAlbedo, 1.0);
#else
    outputImage[pixel] = float4(result.directRadiance + result.indirectRadiance + result.emission, 1.0);
#endif
    depthImage[pixel]  = result.primaryDepth;
    normalImage[pixel] = float4(result.primaryNormal, 0.0);
}

PathResult tracePath(
    RayDesc               ray,
    uint                  primaryMask,
    uint                  primaryFlags,
    inout uint            state,
    out RestirSurfaceData primarySurface,
    out RestirReservoir   primaryReservoir
)
{
    PathResult result = createPathResult();
    primarySurface    = {};
    primaryReservoir  = {};
    float3 throughput = float3(1.0);

    for (uint bounce = 0; bounce < parameters.maxBounces; ++bounce)
    {
        PathHit hit   = {};
        uint    mask  = bounce == 0 ? primaryMask : kSceneMask | kLightMask;
        uint    flags = bounce == 0 ? primaryFlags : RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
        TraceRay(scene, flags, mask, 0, 0, 0, ray, hit);

        if (hit.event == PathEvent::Miss)
        {
            if (bounce == 0 && parameters.iblEnabled != 0)
            {
                result.emission = radianceMap.SampleLevel(radianceSampler, ray.Direction, 0.0).rgb;
            }
            break;
        }
        if (hit.event == PathEvent::Emitter)
        {
            if (bounce == 0 && dot(lights[hit.lightIndex].direction.xyz, -ray.Direction) > 0.0)
            {
                result.emission = lights[hit.lightIndex].color;
            }
            break;
        }

        Surface surface = hit.surface;
        if (bounce == 0)
        {
            result.primaryDepth  = linearViewDepth(surface.position, sceneUniforms.view);
            result.primaryNormal = surface.geometricNormal;
            result.primaryAlbedo = surface.albedo();
            result.emission      = surface.emissive;
            primarySurface       = restirPackSurface(surface, result.primaryDepth);
            // Use the stored material and directions consistently in initial and reuse target functions.
            primaryReservoir = restirGenerateInitial(restirUnpackSurface(primarySurface), state);
        }
        else
        {
            RestirReservoir reservoir = restirGenerateInitial(surface, state);
            result.indirectRadiance += throughput * restirShade(surface, reservoir);
        }
        if (bounce + 1 == parameters.maxBounces)
        {
            break;
        }

        BrdfSample sampled = surface.sample(state);
        if (!advancePath(surface, sampled, bounce + 1, throughput, ray, state))
            break;
    }
    return result;
}

[shader("raygeneration")] void raygenMain()
{
    uint2             pixel            = DispatchRaysIndex().xy;
    uint              state            = pathRandomState(pixel, DispatchRaysDimensions().x);
    PathResult        result           = createPathResult();
    RestirSurfaceData primarySurface   = {};
    RestirReservoir   primaryReservoir = {};
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = createPathRay(pixel, float2(0.5));
        result =
            tracePath(ray, kSceneMask | kLightMask, RAY_FLAG_FORCE_OPAQUE, state, primarySurface, primaryReservoir);
    }
    uint index                     = pixel.y * restirFrame.width + pixel.x;
    restirSurfaces[index]          = primarySurface;
    restirInitialReservoirs[index] = primaryReservoir;
    writePathResult(pixel, result);
}

#include "restir_reuse.slang"
