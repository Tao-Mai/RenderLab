#ifndef RENDERLAB_POINT_LIGHT_SAMPLING_SLANG
#define RENDERLAB_POINT_LIGHT_SAMPLING_SLANG

#include "light_data.slang"
#include "random.slang"

struct PointLightSample
{
    uint index;
    float probability; // Discrete light-selection PMF, not a solid-angle PDF. Zero means no sample.
};

PointLightSample samplePointLight(
    StructuredBuffer<LightData> lights, uint begin, uint end, float3 position, inout uint state)
{
    PointLightSample selected = {0, 0.0};
    float selectedWeight = 0.0;
    float weightSum = 0.0;

    for (uint index = begin; index < end; ++index)
    {
        LightData light = lights[index];

        float3 offset = light.position - position;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0)
            continue;

        float weight = max(light.color.r, max(light.color.g, light.color.b)) / distanceSquared;
        if (weight <= 0.0)
            continue;

        weightSum += weight;
        if (randomFloat(state) < weight / weightSum)
        {
            selected.index = index;
            selectedWeight = weight;
        }
    }

    if (selectedWeight > 0.0)
        selected.probability = selectedWeight / weightSum;
    return selected;
}

#endif
