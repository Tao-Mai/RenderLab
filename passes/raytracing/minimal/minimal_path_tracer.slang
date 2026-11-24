#include "raytracing/light_sampling.slang"

[format("rgba16f")] RWTexture2D<float4> outputImage;

// One ordinary light sample and one BRDF continuation per scattering event.
float3 evalDirectLight(Surface surface, inout uint state)
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

float3 tracePath(RayDesc ray, inout uint state)
{
    float3  directRadiance   = float3(0.0);
    float3  indirectRadiance = float3(0.0);
    float3  cameraEmission   = float3(0.0);
    float3  throughput       = float3(1.0);
    Surface previousSurface  = {};
    float   previousBrdfPdf  = 0.0;

    // maxBounces counts surface scattering events; the final ray still sees emission and the environment.
    for (uint bounce = 0; bounce <= parameters.maxBounces; ++bounce)
    {
        PathHit hit = {};
        uint flags  = bounce == 0 ? RAY_FLAG_FORCE_OPAQUE : RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
        TraceRay(scene, flags, kSceneMask | kLightMask, 0, 0, 0, ray, hit);

        float3 emission = hitEmission(ray, hit);
        if (bounce == 0)
            cameraEmission = emission;
        else
        {
            float3 contribution =
                throughput * emission * simpleEmissionWeight(previousSurface, ray, hit, previousBrdfPdf);
            if (bounce == 1)
                directRadiance += contribution;
            else
                indirectRadiance += contribution;
        }
        if (hit.event != PathEvent::Surface || bounce == parameters.maxBounces)
            break;

        Surface surface = hit.surface;
        if (bounce == 0)
            directRadiance = evalDirectLight(surface, state);
        else
            indirectRadiance += throughput * evalDirectLight(surface, state);

        BrdfSample sampled = surface.sample(state);
        if (!advancePath(surface, sampled, bounce + 1, throughput, ray, state))
            break;
        previousSurface = surface;
        previousBrdfPdf = sampled.pdf;
    }
    return directRadiance + indirectRadiance + cameraEmission;
}

[shader("raygeneration")]
void raygenMain()
{
    uint2  pixel = DispatchRaysIndex().xy;
    uint   state = pathRandomState(pixel, DispatchRaysDimensions().x);
    float3 color = float3(0.0);
    if (parameters.containsPixel(pixel))
        color = tracePath(createPathRay(pixel, float2(0.5)), state);
    outputImage[pixel] = float4(color, 1.0);
}
