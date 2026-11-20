#include "output.slang"
#include "light_sampling.slang"

// One ordinary light sample and one BRDF continuation per scattering event.
float3 simpleDirectLight(Surface surface, inout uint state)
{
    RealtimeLightSample sample = sampleLight(state);
    if (sample.kind == kInvalidLightSample)
        return float3(0.0);

    RealtimeLightEvaluation evaluated = evaluateLightSample(surface, sample);
    if (evaluated.target <= 0.0 || !surface.isVisible(evaluated.direction, evaluated.distance))
        return float3(0.0);

    float selectionPdf = lightSelectionPdf(sample);
    float lightPdf     = selectionPdf * evaluated.conditionalPdf;
    float brdfPdf     = evaluated.delta != 0 ? 0.0 : surface.pdf(evaluated.direction);
    // Balance MIS also weights the BRDF ray when it reaches an emitter or the environment.
    float weight = lightPdf / (lightPdf + brdfPdf);
    return evaluated.contribution * (weight / selectionPdf);
}

float simpleEmissionWeight(Surface previousSurface, RayDesc ray, PathHit hit, float brdfPdf)
{
    RealtimeLightSample sample = lightSampleAtHit(ray, hit);
    if (sample.kind == kInvalidLightSample)
        return 1.0;

    RealtimeLightEvaluation evaluated = evaluateLightSample(previousSurface, sample);
    float                   lightPdf  = lightSelectionPdf(sample) * evaluated.conditionalPdf;
    return brdfPdf / (brdfPdf + lightPdf);
}

PathResult traceSimplePath(RayDesc ray, inout uint state)
{
    PathResult result          = createPathResult();
    float3     throughput      = float3(1.0);
    Surface    previousSurface = {};
    float      previousBrdfPdf = 0.0;

    // maxBounces counts surface scattering events; the final ray still sees emission and the environment.
    for (uint bounce = 0; bounce <= parameters.maxBounces; ++bounce)
    {
        PathHit hit = {};
        uint flags  = bounce == 0 ? RAY_FLAG_FORCE_OPAQUE : RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
        TraceRay(scene, flags, kSceneMask | kLightMask, 0, 0, 0, ray, hit);

        float3 emission = hitEmission(ray, hit);
        if (bounce == 0)
            result.emission = emission;
        else
        {
            float3 contribution =
                throughput * emission * simpleEmissionWeight(previousSurface, ray, hit, previousBrdfPdf);
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
            result.primaryDepth   = linearViewDepth(surface.position, sceneUniforms.view);
            result.primaryNormal  = surface.geometricNormal;
            result.primaryAlbedo  = surface.albedo();
            result.directRadiance = simpleDirectLight(surface, state);
        }
        else
            result.indirectRadiance += throughput * simpleDirectLight(surface, state);

        BrdfSample sampled = surface.sample(state);
        if (!advancePath(surface, sampled, bounce + 1, throughput, ray, state))
            break;
        previousSurface = surface;
        previousBrdfPdf = sampled.pdf;
    }
    return result;
}

[shader("raygeneration")] 
void raygenMain()
{
    uint2      pixel  = DispatchRaysIndex().xy;
    uint       state  = pathRandomState(pixel, DispatchRaysDimensions().x);
    PathResult result = createPathResult();
    if (parameters.containsPixel(pixel))
        result = traceSimplePath(parameters.createRay(pixel, float2(0.5)), state);
    writePathResult(pixel, result);
}
