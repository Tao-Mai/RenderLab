#ifndef RENDERLAB_RECT_AREA_LIGHT_SAMPLING_SLANG
#define RENDERLAB_RECT_AREA_LIGHT_SAMPLING_SLANG

#include "light_data.slang"
#include "random.slang"

// Sampling is analytic; the emitter's two TLAS triangles handle intersection.
struct RectAreaLight
{
    LightData data;

    float selectionWeight(float3 origin)
    {
        float3 toSurface = origin - data.position;
        float distanceSquared = dot(toSurface, toSurface);
        if (distanceSquared == 0.0 || any(data.areaSize <= 0.0))
            return 0.0;

        float lightCosine = saturate(dot(data.direction.xyz, toSurface * rsqrt(distanceSquared)));
        float projectedArea = data.areaSize.x * data.areaSize.y * lightCosine;
        return projectedArea * max(0.0, max(data.color.r, max(data.color.g, data.color.b))) / distanceSquared;
    }

    float3 samplePosition(inout uint state)
    {
        float u = (randomFloat(state) - 0.5) * data.areaSize.x;
        float v = (randomFloat(state) - 0.5) * data.areaSize.y;
        float3 up = normalize(cross(data.direction.xyz, data.areaRight));
        return data.position + data.areaRight * u + up * v;
    }

    // Conditional solid-angle PDF after selecting this light.
    float pdf(float3 origin, float3 target)
    {
        float3 offset = target - origin;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0 || any(data.areaSize <= 0.0))
            return 0.0;

        float lightCosine = -dot(offset * rsqrt(distanceSquared), data.direction.xyz);
        if (lightCosine <= 0.0)
            return 0.0;

        return distanceSquared / (data.areaSize.x * data.areaSize.y * lightCosine);
    }
};

struct RectAreaLightSample
{
    uint index;
    float probability; // Discrete selection PMF; zero means no sample.
    float weightSum; // Reused by the BRDF ray to reconstruct the hit light's selection PMF.
};

RectAreaLightSample sampleRectAreaLight(
    StructuredBuffer<LightData> lights, uint begin, uint end, float3 origin, inout uint state)
{
    RectAreaLightSample selected = {0, 0.0, 0.0};
    float selectedWeight = 0.0;

    for (uint index = begin; index < end; ++index)
    {
        RectAreaLight emitter = {lights[index]};
        float weight = emitter.selectionWeight(origin);
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

#endif
