#ifndef RENDERLAB_PATH_TRACER_LIGHTING_SLANG
#define RENDERLAB_PATH_TRACER_LIGHTING_SLANG

#include "raytracing/path_tracing.slang"
#include "Rendering/Lights/LightHelpers.slang"
#include "Rendering/Lights/EmissiveLightSamplerInterface.slang"
#include "Rendering/Lights/EnvMapSampler.slang"
#include "math/random.slang"

uint getLightSamplingGroupCount()
{
    return uint(USE_ANALYTIC_LIGHTS && scene.getLightCount() > 0)
        + uint(USE_EMISSIVE_LIGHTS && emissiveLights.getTriangleCount() > 0) + uint(HAS_ENV);
}

float3 evalDirectLighting<EmissiveSampler : IEmissiveLightSampler>(
    EnvMapSampler envMapSampler, EmissiveSampler emissiveSampler, Surface surface,
    inout SampleGenerator state)
{
    const uint groupCount = getLightSamplingGroupCount();
    if (groupCount == 0)
        return float3(0.0);

    const uint analyticCount = scene.getLightCount();
    const uint analyticGroups = uint(USE_ANALYTIC_LIGHTS && analyticCount > 0);
    const uint emissiveGroups = uint(USE_EMISSIVE_LIGHTS && emissiveLights.getTriangleCount() > 0);
    const uint group = randomIndex(groupCount, state);
    const float groupPdf = 1.0 / float(groupCount);
    const float3 origin = surface.rayOrigin();
    float3 direction;
    float distance;
    float3 radiance;
    float lightPdf;
    bool useBrdfMis = true;

    if (group < analyticGroups)
    {
        const uint lightIndex = randomIndex(analyticCount, state);

        AnalyticLightSample sample;
        if (!sampleLight(origin, scene.analyticLights[lightIndex], state, sample))
            return float3(0.0);

        direction = sample.direction;
        distance = sample.distance;
        // Delta lights have zero solid-angle PDF and only divide by discrete light selection.
        const float conditionalPdf = sample.pdf == 0.0 ? 1.0 : sample.pdf;
        lightPdf = groupPdf * conditionalPdf / float(analyticCount);
        radiance = sample.radiance;

        // Analytic lights have no geometry for a competing BRDF hit.
        useBrdfMis = false;
    }
    else if (group < analyticGroups + emissiveGroups)
    {
        TriangleLightSample sample;
        if (!emissiveSampler.sampleLight(origin, state, sample))
            return float3(0.0);

        direction = sample.direction;
        distance = sample.distance;
        lightPdf = groupPdf * sample.pdf;
        radiance = sample.radiance;
    }
    else
    {
        EnvMapSample sample;
        if (!envMapSampler.sample(sampleNext2D(state), sample))
            return float3(0.0);

        direction = sample.direction;
        distance = kMaxRayDistance;
        lightPdf = groupPdf * sample.pdf;
        radiance = sample.radiance;
    }

    const float brdfPdf = useBrdfMis ? surface.pdf(direction) : 0.0;
    const float weight = lightPdf / (lightPdf + brdfPdf);
    const float3 weightedRadiance = radiance * (weight / lightPdf);
    const float3 contribution = surface.evaluateLight(direction, weightedRadiance);
    if (!any(contribution > 0.0) || !surface.isVisible(direction, distance))
        return float3(0.0);

    return contribution;
}

float evalEmissionMisWeight<EmissiveSampler : IEmissiveLightSampler>(
    EnvMapSampler envMapSampler, EmissiveSampler emissiveSampler, Surface previousSurface,
    RayDesc ray, PathHit hit, float brdfPdf)
{
    if (brdfPdf == 0.0)
        return 1.0;

    float lightPdf;
    if (hit.event == PathEvent::Surface)
    {
        if (!USE_EMISSIVE_LIGHTS || emissiveLights.getTriangleCount() == 0 ||
            hit.surface.emissiveTriangleIndex == kInvalidLightIndex)
            return 1.0;

        const TriangleLightHit lightHit = {hit.surface.emissiveTriangleIndex, hit.surface.position};
        lightPdf = emissiveSampler.evalPdf(previousSurface.rayOrigin(), lightHit);
    }
    else
    {
#if HAS_ENV
        lightPdf = envMapSampler.evalPdf(ray.Direction);
#else
        return 1.0;
#endif
    }

    lightPdf /= float(getLightSamplingGroupCount());

    return brdfPdf / (brdfPdf + lightPdf);
}

#endif
