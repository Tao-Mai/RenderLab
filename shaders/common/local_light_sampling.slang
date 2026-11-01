#ifndef RENDERLAB_LOCAL_LIGHT_SAMPLING_SLANG
#define RENDERLAB_LOCAL_LIGHT_SAMPLING_SLANG

#include "light_data.slang"
#include "random.slang"

struct LocalLightSample
{
    uint index;
    float probability; // Discrete light-selection PMF, not a solid-angle PDF. Zero means no sample.
    float3 direction;
    float3 incidentIrradiance;
};

LocalLightSample sampleLocalLight(
    StructuredBuffer<LightData> lights, LightRanges ranges, float3 position, inout uint state)
{
    LocalLightSample selected = {0, 0.0, float3(0.0), float3(0.0)};
    float selectedWeight = 0.0;
    float weightSum = 0.0;

    // Point and Spot share one reservoir; skip the RectArea group between them.
    uint pointCount = ranges.pointEnd - ranges.directionalEnd;
    uint localCount = pointCount + ranges.spotEnd - ranges.rectAreaEnd;
    for (uint candidate = 0; candidate < localCount; ++candidate)
    {
        uint index = candidate < pointCount
            ? ranges.directionalEnd + candidate : ranges.rectAreaEnd + candidate - pointCount;
        LightData light = lights[index];

        float3 offset = light.position - position;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0)
            continue;

        float distanceToLight = sqrt(distanceSquared);
        if (distanceToLight > light.range)
            continue;

        float3 lightDirection = offset / distanceToLight;
        float attenuation = light.type == LightType::Spot ? spotAngularAttenuation(light, -lightDirection) : 1.0;
        if (attenuation <= 0.0)
            continue;

        float3 incidentIrradiance = light.color * (attenuation / distanceSquared);
        float weight = max(incidentIrradiance.r, max(incidentIrradiance.g, incidentIrradiance.b));
        if (weight <= 0.0)
            continue;

        weightSum += weight;
        if (randomFloat(state) < weight / weightSum)
        {
            selected.index = index;
            selected.direction = lightDirection;
            selected.incidentIrradiance = incidentIrradiance;
            selectedWeight = weight;
        }
    }

    if (selectedWeight > 0.0)
        selected.probability = selectedWeight / weightSum;
    return selected;
}

#endif
