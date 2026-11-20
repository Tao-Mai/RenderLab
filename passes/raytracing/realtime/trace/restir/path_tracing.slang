#include "passes/raytracing/realtime/trace/output.slang"
#include "passes/raytracing/realtime/trace/restir/restir.slang"

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
        RayDesc ray = parameters.createRay(pixel, float2(0.5));
        result =
            tracePath(ray, kSceneMask | kLightMask, RAY_FLAG_FORCE_OPAQUE, state, primarySurface, primaryReservoir);
    }
    uint index                     = pixel.y * restirFrame.width + pixel.x;
    restirSurfaces[index]          = primarySurface;
    restirInitialReservoirs[index] = primaryReservoir;
    writePathResult(pixel, result);
}

#include "passes/raytracing/realtime/trace/restir/restir_reuse.slang"
