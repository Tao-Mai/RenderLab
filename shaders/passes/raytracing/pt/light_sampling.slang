#ifndef RENDERLAB_PT_LIGHT_SAMPLING_SLANG
#define RENDERLAB_PT_LIGHT_SAMPLING_SLANG

#include "raytracing/analytic_light_sampling.slang"
#include "scene/emissive_lights.slang"

struct AnalyticReservoir
{
    AnalyticLightSample sample;
    float weight;
    float weightSum;

    property probability : float { get { return weightSum > 0.0 ? weight / weightSum : 0.0; } }

    [mutating] void consider(AnalyticLightSample candidate, inout uint state)
    {
        float3 incident = candidate.radiance / candidate.pdf;
        float candidateWeight = max(incident.x, max(incident.y, incident.z));
        if (candidateWeight <= 0.0)
            return;
        weightSum += candidateWeight;
        if (randomFloat(state) < candidateWeight / weightSum)
        {
            sample = candidate;
            weight = candidateWeight;
        }
    }
};

void sampleAnalyticLights(float3 origin, inout uint state, out AnalyticReservoir local, out AnalyticReservoir distant)
{
    local = {};
    distant = {};
    for (uint index = 0; index < scene.analyticLightCount; ++index)
    {
        LightData light = scene.analyticLights[index];
        float2 uv = float2(0.0);
        if (light.type == LightType::RectArea)
        {
            uv.x = randomFloat(state);
            uv.y = randomFloat(state);
        }
        AnalyticLightSample candidate;
        if (!sampleAnalyticLight(light, origin, uv, candidate))
            continue;
        if (light.type == LightType::Directional)
            distant.consider(candidate, state);
        else
            local.consider(candidate, state);
    }
}

float emissiveSelectionWeight(uint lightIndex, float3 origin)
{
    EmissiveTriangle triangle = emissiveLights.triangles[lightIndex];
    float3 center = (triangle.p0 + triangle.vertex1 + triangle.vertex2) / 3.0;
    float3 offset = origin - center;
    float distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0 || triangle.power == 0.0)
        return 0.0;

    float cosine = dot(triangle.geometricNormal, offset * rsqrt(distanceSquared));
    float projectedCosine = emissiveTwoSided(triangle) ? abs(cosine) : max(0.0, cosine);
    return triangle.power * projectedCosine / distanceSquared;
}

struct EmissiveLightSample
{
    uint index;
    float probability;
    float weightSum;
};

EmissiveLightSample sampleEmissiveLight(float3 origin, inout uint state)
{
    EmissiveLightSample selected = {};
    float selectedWeight = 0.0;
    for (uint index = 0; index < emissiveLights.triangleCount; ++index)
    {
        float weight = emissiveSelectionWeight(index, origin);
        if (weight <= 0.0)
            continue;
        selected.weightSum += weight;
        if (randomFloat(state) < weight / selected.weightSum)
        {
            selected.index = index;
            selectedWeight = weight;
        }
    }
    if (selectedWeight > 0.0)
        selected.probability = selectedWeight / selected.weightSum;
    return selected;
}

float emissivePdf(uint lightIndex, float3 origin, float3 target)
{
    EmissiveTriangle triangle = emissiveLights.triangles[lightIndex];
    float3 offset = target - origin;
    float distanceSquared = dot(offset, offset);
    if (triangle.area == 0.0 || distanceSquared == 0.0)
        return 0.0;

    float cosine = dot(triangle.geometricNormal, -offset * rsqrt(distanceSquared));
    float projectedCosine = emissiveTwoSided(triangle) ? abs(cosine) : max(0.0, cosine);
    return projectedCosine > 0.0 ? distanceSquared / (triangle.area * projectedCosine) : 0.0;
}

#endif
