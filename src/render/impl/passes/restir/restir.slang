#include "raytracing/alpha_test.slang"
#include "Rendering/Lights/EmissiveUniformSampler.slang"
#include "raytracing/visibility_buffer.slang"
#include "raytracing/path_tracing.slang"
#include "Rendering/Lights/LightHelpers.slang"
#include "Rendering/Lights/EnvMapSampler.slang"
#include "math/color.slang"

struct PathTracingUniforms
{
    uint sampleSeed;
};

struct ReservoirHistory
{
    int selectedLightIndex;
    float3 selectedContribution;
    float selectedTarget;
    float weightSum;
    uint sampleCount;
};

static const uint kCandidateCount = 5;

ConstantBuffer<PathTracingUniforms> uniforms;
RWStructuredBuffer<ReservoirHistory> reservoirHistory;

Texture2D<uint4> vbuffer;


[format("rgba32f")] RWTexture2D<float4> outputImage;


float3 evalDL(Surface surface, inout SampleGenerator state) {
    uint lightCount = scene.getLightCount();
    if (lightCount == 0)
        return float3(0.0);

    // TODO 选灯pdf感觉可以用分层采样
    int selectedLightIndex = -1;
    float3 selectedContribution = 0.0;
    float selectedTarget = 1.0;
    
    float reservoir = 0.0;
    uint sampleCount = 0;

    uint2 pixel = DispatchRaysIndex().xy;
    uint historyIndex = pixel.x + pixel.y * DispatchRaysDimensions().x;
    
    if (uniforms.sampleSeed > 0)
    {
        ReservoirHistory history = reservoirHistory[historyIndex];
        selectedLightIndex = history.selectedLightIndex;
        selectedContribution = history.selectedContribution;
        selectedTarget = history.selectedTarget;
        reservoir = history.weightSum;
        sampleCount = history.sampleCount;
    }

    for (uint i = 0; i < kCandidateCount; i++) {
        uint lightIndex = min(uint(sampleNext1D(state) * lightCount), lightCount - 1u);
        float proposalPdf = 1.0f / float(lightCount);

        AnalyticLightSample result;

        if (!sampleLight(surface.rayOrigin(), scene.analyticLights[lightIndex], state, result)) {
            continue;
        }

        // Delta lights only divide by the discrete light selection probability.
     //   float conditionalPdf = result.pdf == 0.0 ? 1.0 : result.pdf;

        // L F G / proposal pdf
        float3 contribution = surface.evaluateLight(result.direction, result.radiance);

        if (!any(contribution > 0.0) || !surface.isVisible(result.direction, result.distance)) {
             continue;
        }

        float target = rec709Luminance(contribution);
        float wi = target / proposalPdf;
        reservoir += wi;

        if (sampleNext1D(state) * reservoir < wi) {
            selectedLightIndex = lightIndex;
            selectedContribution = contribution;
            selectedTarget = target;
        }
    }

    sampleCount += kCandidateCount;
    reservoirHistory[historyIndex] = {
        selectedLightIndex, selectedContribution, selectedTarget, reservoir, sampleCount};

    return selectedContribution * reservoir / float(sampleCount) / selectedTarget;
}

float3 tracePath(RayDesc ray, PathHit hit, inout SampleGenerator state)
{
    float3  radiance         = float3(0.0);

    EnvMapSampler envMapSampler = {};

    if (hit.event != PathEvent::Surface) {
         return envMapSampler.eval(ray.Direction);
    }

    Surface surface = hit.surface;
    radiance += surface.emissive; 

    radiance += evalDL(surface, state);
    
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
    SampleGenerator state = createPathSampleGenerator(pixel, frameDim.x, uniforms.sampleSeed);
    RayDesc ray = createCameraRay(pixel, frameDim, float2(0.5));
    VBufferHit primary = {vbuffer.Load(int3(pixel, 0))};

    PathHit hit = {};
    if (primary.isValid())
    {
        hit.event = PathEvent::Surface;
        hit.surface = surfaceAt(primary.instanceIndex, primary.primitiveIndex, primary.barycentrics, ray.Direction);
    }

    outputImage[pixel] = float4(tracePath(ray, hit, state), 1.0);
}
