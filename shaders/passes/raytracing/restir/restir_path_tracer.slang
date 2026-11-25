#include "raytracing/path_tracing.slang"
#include "raytracing/LightingLayers.h"
#include "restir.slang"

#if SVGF
[format("rgba16f")] RWTexture2DArray<float4> outputImage;
[format("rgba16f")] RWTexture2D<float4> emissionImage;
[format("rgba16f")] RWTexture2D<float4> albedoImage;
#else
[format("rgba16f")] RWTexture2D<float4> outputImage;
#endif
[format("r32f")] RWTexture2D<float> depthImage;
[format("rgba16f")] RWTexture2D<float4> normalImage;

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
    bool previousDelta = false;

    for (uint bounce = 0; bounce <= parameters.maxBounces; ++bounce)
    {
        PathHit hit   = {};
        uint    flags = bounce == 0 ? primaryFlags : RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
        TraceRay(scene.rtAccel, flags, kSceneMask, 0, 0, 0, ray, hit);

        if (bounce == 0)
            result.emission = hitEmission(ray, hit);
        else if (previousDelta)
        {
            float3 contribution = throughput * hitEmission(ray, hit);
            if (bounce == 1)
                result.directRadiance += contribution;
            else
                result.indirectRadiance += contribution;
        }
        if (hit.event != PathEvent::Surface || bounce == parameters.maxBounces)
            break;

        Surface surface = hit.surface;
        if (bounce == 0)
        {
            result.primaryDepth  = linearViewDepth(surface.position, scene.camera.view);
            result.primaryNormal = surface.geometricNormal;
            result.primaryAlbedo = surface.albedo();
            primarySurface       = restirPackSurface(surface, result.primaryDepth);
            // Use the stored material and directions consistently in initial and reuse target functions.
            primaryReservoir = restirGenerateInitial(restirUnpackSurface(primarySurface), state);
        }
        else
        {
            RestirReservoir reservoir = restirGenerateInitial(surface, state);
            result.indirectRadiance += throughput * restirShade(surface, reservoir);
        }
        if (bounce + 1 == parameters.maxBounces && !surface.scattering.isDelta())
            break;
        pbrt::BsdfSample sampled = surface.sample(state);
        if (!advancePath(surface, sampled, bounce + 1, throughput, ray, state))
            break;
        previousDelta = sampled.delta != 0;
    }
    return result;
}

[shader("raygeneration")] void raygenMain()
{
    uint2             pixel            = DispatchRaysIndex().xy;
    uint2             frameDim         = DispatchRaysDimensions().xy;
    SampleGenerator   state            = createPathSampleGenerator(pixel, frameDim.x);
    RestirSurfaceData primarySurface   = {};
    RestirReservoir   primaryReservoir = {};
    RayDesc ray = createCameraRay(pixel, frameDim, float2(0.5));
    PathResult result = tracePath(ray, RAY_FLAG_FORCE_OPAQUE, state, primarySurface, primaryReservoir);
    uint index                     = pixel.y * restirFrame.width + pixel.x;
    restirSurfaces[index]          = primarySurface;
    restirInitialReservoirs[index] = primaryReservoir;
    writePathResult(pixel, result);
}

#include "restir_reuse.slang"
