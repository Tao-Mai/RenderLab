#ifndef RENDERLAB_ANALYTIC_LIGHT_SAMPLING_SLANG
#define RENDERLAB_ANALYTIC_LIGHT_SAMPLING_SLANG

#include "scene/light_data.slang"
#include "math/constant.slang"
#include "math/random.slang"
#include "raytracing/scene_hits.slang"

uint uniformLightIndex(uint count, inout SampleGenerator state)
{
    uint threshold = (0u - count) % count;
    uint value;
    do
    {
        value = randomUint(state);
    }
    while (value < threshold);
    return value % count;
}

struct AnalyticLightSample
{
    float3 position;
    float3 direction;
    float3 radiance;
    float distance;
    float pdf;
};

bool sampleAnalyticLight(LightData light, float3 origin, float2 uv, out AnalyticLightSample result)
{
    result = {};
    if (light.type == LightType::Directional)
    {
        result.direction = -normalize(light.direction.xyz);
        result.radiance = light.color;
        result.distance = kMaxRayDistance;
        result.pdf = 1.0;
        return true;
    }

    bool rectangle = light.type == LightType::RectArea;
    result.position = rectangle ? sampleRectLightPosition(light, uv) : light.position;
    float3 offset = result.position - origin;
    float distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0 || (!rectangle && !withinLightRange(light, distanceSquared)))
        return false;

    result.distance = sqrt(distanceSquared);
    result.direction = offset / result.distance;
    if (rectangle)
    {
        float projectedArea = rectLightArea(light) * dot(light.direction.xyz, -result.direction);
        if (projectedArea <= 0.0)
            return false;
        result.pdf = distanceSquared / projectedArea;
        result.radiance = light.color;
    }
    else
    {
        float attenuation = light.type == LightType::Spot ? spotAngularAttenuation(light, -result.direction) : 1.0;
        result.radiance = light.color * (attenuation / distanceSquared);
        result.pdf = 1.0;
    }
    return true;
}

#endif
