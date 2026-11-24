#ifndef RENDERLAB_PT_LIGHT_SAMPLING_SLANG
#define RENDERLAB_PT_LIGHT_SAMPLING_SLANG

#include "scene/light_emitters.slang"
#include "math/random.slang"

float lightWeight(float3 value)
{
    return max(0.0, max(value.x, max(value.y, value.z)));
}

struct DeltaLightSample
{
    float3 direction;
    float3 incidentIrradiance;
    float distance;
    float probability;
};

// Point and spot lights share one weighted reservoir. Their emission is intensity I.
DeltaLightSample sampleLocalLight(LightRanges ranges, float3 origin, inout uint state)
{
    DeltaLightSample selected = {};
    float selectedWeight = 0.0;
    float weightSum = 0.0;
    uint pointCount = ranges.pointEnd - ranges.directionalEnd;
    uint spotCount = ranges.spotEnd - ranges.rectAreaEnd;
    for (uint candidate = 0; candidate < pointCount + spotCount; ++candidate)
    {
        uint index = candidate < pointCount ? ranges.directionalEnd + candidate
            : ranges.rectAreaEnd + candidate - pointCount;
        LightData light = lights[index];
        float3 offset = light.position - origin;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0 || !withinLightRange(light, distanceSquared))
            continue;

        float distance = sqrt(distanceSquared);
        float3 direction = offset / distance;
        float attenuation = light.type == LightType::Spot ? spotAngularAttenuation(light, -direction) : 1.0;
        float3 incident = light.color * (attenuation / distanceSquared);
        float weight = lightWeight(incident);
        if (weight <= 0.0)
        {
            continue;
        }
        weightSum += weight;
        if (randomFloat(state) < weight / weightSum)
        {
            selected.direction = direction;
            selected.incidentIrradiance = incident;
            selected.distance = distance;
            selectedWeight = weight;
        }
    }
    if (selectedWeight > 0.0)
    {
        selected.probability = selectedWeight / weightSum;
    }

    return selected;
}

DeltaLightSample sampleDistantLight(uint lightCount, inout uint state)
{
    DeltaLightSample selected = {};
    float selectedWeight = 0.0;
    float weightSum = 0.0;
    for (uint index = 0; index < lightCount; ++index)
    {
        LightData light = lights[index];
        float3 incident = light.color;
        float weight = lightWeight(incident);
        if (weight <= 0.0)
        {
            continue;
        }
        weightSum += weight;
        if (randomFloat(state) < weight / weightSum)
        {
            selected.direction = -light.direction.xyz;
            selected.incidentIrradiance = incident;
            selected.distance = kMaxRayDistance;
            selectedWeight = weight;
        }
    }
    if (selectedWeight > 0.0)
    {
        selected.probability = selectedWeight / weightSum;
    }

    return selected;
}

float areaSelectionWeight(uint lightIndex, float3 origin)
{
    LightTriangle triangle = loadTriangleLight(lightIndex);
    float3 center = (triangle.p0 + triangle.vertex1 + triangle.vertex2) / 3.0;
    float3 offset = origin - center;
    float distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0 || triangle.area == 0.0)
    {
        return 0.0;
    }

    float cosine = dot(triangle.geometricNormal, offset * rsqrt(distanceSquared));
    float projectedCosine = triangle.twoSided != 0 ? abs(cosine) : max(0.0, cosine);
    LightEmission emission = areaEmissions[triangle.emissionIndex];
    // A texture can be bright away from its centroid. Keep support over the whole triangle.
    float intensity = lightWeight(averageEmission(emission));

    return triangle.area * projectedCosine * intensity / distanceSquared;
}

struct AreaLightSample
{
    uint index;
    float probability;
    float weightSum;
};

AreaLightSample sampleAreaLight(uint lightCount, float3 origin, inout uint state)
{
    AreaLightSample selected = {};
    float selectedWeight = 0.0;
    for (uint index = 0; index < lightCount; ++index)
    {
        float weight = areaSelectionWeight(index, origin);
        if (weight <= 0.0)
        {
            continue;
        }
        selected.weightSum += weight;
        if (randomFloat(state) < weight / selected.weightSum)
        {
            selected.index = index;
            selectedWeight = weight;
        }
    }
    if (selectedWeight > 0.0)
    {
        selected.probability = selectedWeight / selected.weightSum;
    }

    return selected;
}

float areaPdf(uint lightIndex, float3 origin, float3 target)
{
    LightTriangle triangle = loadTriangleLight(lightIndex);
    float3 offset = target - origin;
    float distanceSquared = dot(offset, offset);
    if (triangle.area == 0.0 || distanceSquared == 0.0)
    {
        return 0.0;
    }

    float cosine = dot(triangle.geometricNormal, -offset * rsqrt(distanceSquared));
    float projectedCosine = triangle.twoSided != 0 ? abs(cosine) : max(0.0, cosine);
    if (projectedCosine == 0.0)
    {
        return 0.0;
    }

    return distanceSquared / (triangle.area * projectedCosine);
}

#endif
