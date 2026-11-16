#ifndef RENDERLAB_RESTIR_SLANG
#define RENDERLAB_RESTIR_SLANG

#include "raytracing/realtime/restir/restir_data.slang"

RestirSurfaceData restirPackSurface(Surface surface, float depth)
{
    RestirSurfaceData result = {};
    result.position          = surface.position;
    result.depth             = depth;
    result.normal            = surface.normal;
    result.geometricNormal   = surface.geometricNormal;
    result.viewDirection     = surface.viewDirection;
    result.brdf              = surface.brdf;
    result.materialIndex     = surface.materialIndex;
    result.objectIndex       = surface.objectIndex;
    return result;
}

Surface restirUnpackSurface(RestirSurfaceData data)
{
    Surface result               = {};
    result.position              = data.position;
    result.normal                = data.normal;
    result.geometricNormal       = data.geometricNormal;
    result.viewDirection         = data.viewDirection;
    result.brdf                  = data.brdf;
    result.materialIndex         = data.materialIndex;
    result.objectIndex           = data.objectIndex;
    result.emissiveTriangleIndex = kInvalidEmissiveTriangle;
    return result;
}

float restirMixturePdf(Surface surface, RealtimeLightSample sample, RealtimeLightEvaluation evaluated)
{
    float brdfPdf = 0.0;
    if (evaluated.delta == 0 && evaluated.conditionalPdf > 0.0 &&
        dot(surface.geometricNormal, evaluated.direction) > 0.0)
    {
        brdfPdf =
            surface.brdf.pdf(surface.normal, surface.viewDirection, evaluated.direction) / evaluated.conditionalPdf;
    }

    float lightCount = float(restirFrame.candidateCount);
    return (lightCount * lightSelectionPdf(sample) + brdfPdf) / (lightCount + 1.0);
}

RealtimeLightSample restirSampleBrdf(Surface surface, inout uint state)
{
    RealtimeLightSample result  = {};
    BrdfSample          sampled = sampleSurfaceBrdf(surface, state);
    if (sampled.pdf <= 0.0 || dot(surface.geometricNormal, sampled.direction) <= 0.0 || !any(sampled.weight > 0.0))
    {
        return result;
    }

    RayDesc ray;
    ray.Origin    = surface.rayOrigin();
    ray.Direction = sampled.direction;
    ray.TMin      = kRayEpsilon;
    ray.TMax      = kMaxRayDistance;
    PathHit hit   = {};
    TraceRay(
        scene,
        RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        kSceneMask | kLightMask,
        0,
        0,
        0,
        ray,
        hit
    );
    return lightSampleAtHit(ray, hit);
}

void restirDiscardOccluded(Surface surface, inout RestirReservoir reservoir)
{
    if (reservoir.valid)
    {
        RealtimeLightEvaluation evaluated = evaluateLightSample(surface, reservoir.sample);
        if (evaluated.target <= 0.0 || !surface.isVisible(evaluated.direction, evaluated.distance))
        {
            reservoir.weightSum = 0.0;
        }
    }
}

RestirReservoir restirGenerateInitial(Surface surface, inout uint state)
{
    RestirReservoir reservoir = {};
    for (uint candidate = 0; candidate <= restirFrame.candidateCount; ++candidate)
    {
        RealtimeLightSample sample =
            candidate == restirFrame.candidateCount ? restirSampleBrdf(surface, state) : sampleLight(state);
        RealtimeLightEvaluation evaluated = evaluateLightSample(surface, sample);
        float sourcePdf = sample.kind != kInvalidLightSample ? restirMixturePdf(surface, sample, evaluated) : 0.0;
        float weight    = sourcePdf > 0.0 ? evaluated.target / sourcePdf : 0.0;
        reservoir.stream(sample, evaluated.target, weight, 1, state);
    }

    reservoir.finalize(float(reservoir.sampleCount));

    // Original paper, Algorithm 5: discard an occluded initial sample but retain M.
    restirDiscardOccluded(surface, reservoir);
    return reservoir;
}

float3 restirShade(Surface surface, inout RestirReservoir reservoir)
{
    if (!reservoir.valid)
    {
        return float3(0.0);
    }

    RealtimeLightEvaluation evaluated = evaluateLightSample(surface, reservoir.sample);
    if (evaluated.target <= 0.0 || !surface.isVisible(evaluated.direction, evaluated.distance))
    {
        reservoir.weightSum = 0.0;
        return float3(0.0);
    }

    return evaluated.contribution * reservoir.weightSum;
}

#endif
