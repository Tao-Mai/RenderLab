#include "raytracing/alpha_test.slang"
#include "raytracing/light_sampling.slang"

[format("rgba32f")] RWTexture2D<float4> outputImage;
static const uint kScatterHitGroup = 0;
static const uint kVisibilityHitGroup = 1;

// One ordinary light sample and one BRDF continuation per scattering event.
float3 evalDirectLight(Surface surface, inout SampleGenerator state)
{
    if (surface.scattering.isDelta())
        return float3(0.0);

    PathLightSample sample = sampleLight(state);
    if (sample.kind == kInvalidLightSample)
        return float3(0.0);

    PathLightEvaluation evaluated = evaluateLightSample(surface, sample, true);
    if (evaluated.target <= 0.0 ||
        !surface.isVisible(evaluated.direction, evaluated.distance, kVisibilityHitGroup, RAY_FLAG_FORCE_NON_OPAQUE))
        return float3(0.0);

    float selectionPdf = lightSelectionPdf(sample);
    float lightPdf     = selectionPdf * evaluated.conditionalPdf;
    float brdfPdf     = sample.kind == kAnalyticLightSample ? 0.0 : surface.pdf(evaluated.direction);
    // Balance MIS also weights the BRDF ray when it reaches an emitter or the environment.
    float weight = lightPdf / (lightPdf + brdfPdf);
    return evaluated.contribution * (weight / selectionPdf);
}

float simpleEmissionWeight(Surface previousSurface, RayDesc ray, PathHit hit, float brdfPdf)
{
    PathLightSample sample = lightSampleAtHit(ray, hit);
    if (sample.kind == kInvalidLightSample)
        return 1.0;

    PathLightEvaluation evaluated = evaluateLightSample(previousSurface, sample);
    float                   lightPdf  = lightSelectionPdf(sample) * evaluated.conditionalPdf;
    return brdfPdf / (brdfPdf + lightPdf);
}

float3 tracePath(RayDesc ray, inout SampleGenerator state)
{
    float3  directRadiance   = float3(0.0);
    float3  indirectRadiance = float3(0.0);
    float3  cameraEmission   = float3(0.0);
    float3  throughput       = float3(1.0);
    Surface previousSurface  = {};
    float   previousBrdfPdf  = 0.0;
    bool    previousDelta    = false;

    // The primary surface scatters once before counting indirect bounces.
    const uint maxScatteringEvents = parameters.maxBounces + 1u;
    for (uint bounce = 0; bounce <= maxScatteringEvents; ++bounce)
    {
        PathHit hit = {};
        TraceRay(scene.rtAccel, RAY_FLAG_FORCE_NON_OPAQUE, kSceneMask, kScatterHitGroup, 0, 0, ray, hit);

        float3 emission = hitEmission(ray, hit);
        if (bounce == 0) {
            cameraEmission = emission;
        }
        else
        {
            float weight = previousDelta ? 1.0 : simpleEmissionWeight(previousSurface, ray, hit, previousBrdfPdf);
            float3 contribution = throughput * emission * weight;
            if (bounce == 1)
                directRadiance += contribution;
            else
                indirectRadiance += contribution;
        }
        if (hit.event != PathEvent::Surface || bounce == maxScatteringEvents)
            break;

        Surface surface = hit.surface;
        if (bounce == 0)
            directRadiance = evalDirectLight(surface, state);
        else
            indirectRadiance += throughput * evalDirectLight(surface, state);

        pbrt::BsdfSample sampled = surface.sample(state);
        if (!advancePath(surface, sampled, bounce + 1, throughput, ray, state))
            break;
        previousSurface = surface;
        previousBrdfPdf = sampled.pdf;
        previousDelta = sampled.delta != 0;
    }
    return directRadiance + indirectRadiance + cameraEmission;
}

[shader("anyhit")]
void scatterAnyHitMain(inout PathHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    if (isMaskedTriangle(InstanceID(), PrimitiveIndex(), attributes.barycentrics))
        IgnoreHit();
}

[shader("anyhit")]
void visibilityAnyHitMain(inout VisibilityPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (isMaskedTriangle(InstanceID(), PrimitiveIndex(), attributes.barycentrics))
        IgnoreHit();
}

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;
    SampleGenerator state = createPathSampleGenerator(pixel, frameDim.x);
    float3 color = tracePath(createCameraRay(pixel, frameDim, float2(0.5)), state);

    outputImage[pixel] = float4(color, 1.0);
}
