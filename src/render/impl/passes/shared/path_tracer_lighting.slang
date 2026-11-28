#ifndef RENDERLAB_PATH_TRACER_LIGHTING_SLANG
#define RENDERLAB_PATH_TRACER_LIGHTING_SLANG

#include "Rendering/Lights/EmissiveLightSamplerInterface.slang"
#include "Rendering/Lights/EnvMapSampler.slang"
#include "Rendering/Lights/LightHelpers.slang"
#include "passes/shared/path_tracing.slang"

uint getLightSamplingGroupCount()
{
    return uint(USE_ANALYTIC_LIGHTS && gScene.getLightCount() > 0)
        + uint(USE_EMISSIVE_LIGHTS && !gScene.lightCollection.isEmpty()) + uint(HAS_ENV);
}

float3 evalDirectLighting<EmissiveSampler : IEmissiveLightSampler, MaterialInstance : IMaterialInstance>(
    EnvMapSampler envMapSampler, EmissiveSampler emissiveSampler,
    const ShadingData sd, const MaterialInstance mi, inout SampleGenerator state)
{
    const uint groupCount = getLightSamplingGroupCount();
    if (groupCount == 0)
        return float3(0.0);

    const uint analyticCount = gScene.getLightCount();
    const uint analyticGroups = uint(USE_ANALYTIC_LIGHTS && analyticCount > 0);
    const uint emissiveGroups = uint(USE_EMISSIVE_LIGHTS && !gScene.lightCollection.isEmpty());
    const uint group = min(uint(sampleNext1D(state) * groupCount), groupCount - 1u);
    const float groupPdf = 1.0 / float(groupCount);
    const BSDFProperties props = mi.getProperties(sd);
    const bool upperHemisphere = !props.isTransmissive;

    float3 direction;
    float distance;
    float3 radiance;
    float lightPdf;
    bool useBrdfMis = true;

    if (group < analyticGroups)
    {
        const uint lightIndex = min(uint(sampleNext1D(state) * analyticCount), analyticCount - 1u);

        AnalyticLightSample sample;
        if (!sampleLight(sd.posW, gScene.getLight(lightIndex), state, sample))
            return float3(0.0);

        direction = sample.dir;
        distance = sample.distance;
        // AnalyticLightSample.Li is already divided by the within-light sampling PDF.
        lightPdf = groupPdf / float(analyticCount);
        radiance = sample.Li;

        // Analytic lights have no geometry for a competing BRDF hit.
        useBrdfMis = false;
    }
    else if (group < analyticGroups + emissiveGroups)
    {
        TriangleLightSample sample;
        if (!emissiveSampler.sampleLight(sd.posW, props.guideNormal, upperHemisphere, state, sample))
            return float3(0.0);

        direction = sample.dir;
        distance = sample.distance;
        lightPdf = groupPdf * sample.pdf;
        radiance = sample.Le;
    }
    else
    {
        EnvMapSample sample;
        if (!envMapSampler.sample(sampleNext2D(state), sample))
            return float3(0.0);

        direction = sample.dir;
        distance = kMaxLightDistance;
        lightPdf = groupPdf * sample.pdf;
        radiance = sample.Le;
    }

    const float brdfPdf = useBrdfMis ? mi.evalPdf(sd, direction) : 0.0;
    const float weight = lightPdf / (lightPdf + brdfPdf);
    const float3 weightedRadiance = radiance * (weight / lightPdf);
    const float3 contribution = mi.eval(sd, direction, state) * weightedRadiance;
    const float3 shadowOrigin = computeRayOrigin(sd.posW, dot(sd.faceN, direction) >= 0.f ? sd.faceN : -sd.faceN);
    if (!any(contribution > 0.0) || !traceShadowRay(shadowOrigin, direction, distance))
        return float3(0.0);

    return contribution;
}

float evalEmissionMisWeight<EmissiveSampler : IEmissiveLightSampler>(
    EnvMapSampler envMapSampler, EmissiveSampler emissiveSampler,
    const ShadingData previousSd, const float3 previousNormal, const bool upperHemisphere,
    RayDesc ray, const HitInfo hit, const ShadingData hitSd, float brdfPdf)
{
    if (brdfPdf == 0.0)
        return 1.0;

    float lightPdf;
    if (hit.isValid())
    {
        if (!USE_EMISSIVE_LIGHTS || gScene.lightCollection.isEmpty() || hit.getType() != HitType::Triangle)
            return 1.0;

        const TriangleHit triangleHit = hit.getTriangleHit();
        const uint triIdx = gScene.lightCollection.getTriangleIndex(triangleHit.instanceID, triangleHit.primitiveIndex);
        if (triIdx == LightCollection::kInvalidIndex)
            return 1.0;

        const TriangleLightHit lightHit = { triIdx, hitSd.posW, hitSd.faceN };
        lightPdf = emissiveSampler.evalPdf(previousSd.posW, previousNormal, upperHemisphere, lightHit);
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
