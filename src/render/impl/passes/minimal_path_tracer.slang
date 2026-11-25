#include "render/impl/raytracing/alpha_test.slang"
#include "render/impl/raytracing/light_sampling.slang"

struct PathTracingUniforms
{
    uint maxBounces;
    uint sampleIndex;
};

ConstantBuffer<PathTracingUniforms> uniforms;

[format("rgba32f")] RWTexture2D<float4> outputImage;

// One ordinary light sample and one BRDF continuation per scattering event.
float3 evalDirectLight(Surface surface, inout SampleGenerator state)
{
    PathLightSample sample = sampleLight(state);
    if (sample.kind == kInvalidLightSample)
        return float3(0.0);

    PathLightEvaluation evaluated = evaluateLightSample(surface, sample, true);
    if (evaluated.target <= 0.0 ||
        !surface.isVisible(evaluated.direction, evaluated.distance))
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
    float  lightPdf  = lightSelectionPdf(sample) * evaluated.conditionalPdf;
    return brdfPdf / (brdfPdf + lightPdf);
}

float3 tracePath(RayDesc ray, inout SampleGenerator state)
{
    float3  radiance         = float3(0.0);
    float3  throughput       = float3(1.0);
    Surface previousSurface  = {};
    float   previousBrdfPdf  = 0.0;

    const uint maxBounces = uniforms.maxBounces;
    
    for (uint bounce = 0; bounce <= maxBounces; ++bounce)
    {
        PathHit hit = {};
        traceSceneRay(ray, RayType::Scatter, RAY_FLAG_FORCE_NON_OPAQUE, hit);

        if (hit.event != PathEvent::Surface) {
             return environmentRadiance(ray.Direction);
        }

        Surface surface = hit.surface;
        float3 emission = surface.emissive;
        
        if (bounce == 0) 
        {
            radiance += emission; 
        }
        else
        {
            float weight = simpleEmissionWeight(previousSurface, ray, hit, previousBrdfPdf);
            radiance += throughput * emission * weight;
        }

        // 停止反弹，那接收完emission就可以结束，不再eval表面
        if (bounce == maxBounces)
            break;

        // NEE 
        radiance += throughput * evalDirectLight(surface, state);
        
        BrdfSample sampled = surface.sample(state);

        if (sampled.pdf <= 0.0 || dot(surface.geometricNormal, sampled.direction) <= 0.0 ||
            !any(sampled.weight > 0.0))
            break;
    
        throughput *= sampled.weight;

        // 轮盘
        if (bounce >= kRussianRouletteStartBounce)
        {
            float survival = saturate(max(throughput.x, max(throughput.y, throughput.z)));
            if (survival == 0.0 || randomFloat(state) >= survival)
                break;
            throughput /= survival;
        }
        
        ray.Origin    = surface.rayOrigin();
        ray.Direction = sampled.direction;
        ray.TMin      = kRayEpsilon;
        ray.TMax      = kMaxRayDistance;

        previousSurface = surface;
        previousBrdfPdf = sampled.pdf;
    }
    return radiance;
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
    SampleGenerator state = createPathSampleGenerator(pixel, frameDim.x, uniforms.sampleIndex);
    float3 color = tracePath(createCameraRay(pixel, frameDim, float2(0.5)), state);

    outputImage[pixel] = float4(color, 1.0);
}
