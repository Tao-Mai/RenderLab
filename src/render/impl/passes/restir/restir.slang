#include "raytracing/alpha_test.slang"
#include "raytracing/light_sampling.slang"
#include "raytracing/visibility_buffer.slang"

struct PathTracingUniforms
{
    uint maxBounces;
    uint sampleIndex;
};

ConstantBuffer<PathTracingUniforms> uniforms;

Texture2D<uint4> vbuffer;
[format("rgba32f")] RWTexture2D<float4> outputImage;

// One ordinary light sample and one BRDF continuation per scattering event.
float3 evalDirectLight(Surface surface, inout SampleGenerator state)
{
    // uint analyticCount = analyticLightCount();
    // uint index = uniformLightIndex(analyticCount, state);
    
    
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

float3 tracePath(RayDesc ray, PathHit hit, inout SampleGenerator state)
{
    float3  radiance         = float3(0.0);

    if (hit.event != PathEvent::Surface) {
         return environmentRadiance(ray.Direction);
    }

    Surface surface = hit.surface;
    radiance += surface.emissive; 

    radiance += evalDirectLight(surface, state);
    
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
    RayDesc ray = createCameraRay(pixel, frameDim, float2(0.5));
    VBufferHit primary = {vbuffer.Load(int3(pixel, 0))};

    PathHit hit = {};
    if (primary.isValid())
    {
        hit.event = PathEvent::Surface;
        hit.barycentrics = primary.barycentrics;
        hit.surface = surfaceAt(primary.instanceIndex, primary.primitiveIndex, hit.barycentrics, ray.Direction);
    }

    outputImage[pixel] = float4(tracePath(ray, hit, state), 1.0);
}
