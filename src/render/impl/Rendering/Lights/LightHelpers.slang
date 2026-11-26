#ifndef RENDERLAB_LIGHT_HELPERS_SLANG
#define RENDERLAB_LIGHT_HELPERS_SLANG

#include "Scene/Lights/LightData.slang"
#include "Utils/Sampling/SampleGeneratorInterface.slang"
#include "raytracing/scene_hits.slang"

static const float kMinLightDistanceSquared = 1e-9f;

struct AnalyticLightSample
{
    float3 position;
    float3 direction;  // 着色点指向光源
    float3 radiance;  // Unshadowed incident radiance, before division by the PDF.
    float  distance;
    float  pdf;       // 立体角测度下的灯内采样条件概率，delta light为0
};

bool samplePointLight(float3 origin, LightData light, out AnalyticLightSample result)
{
    result = {};
    result.position = light.posW;

    float3 offset = result.position - origin;
    float distanceSquared = max(dot(offset, offset), kMinLightDistanceSquared);
    result.distance = sqrt(distanceSquared);
    result.direction = offset / result.distance;

    float cosine = -dot(result.direction, light.dirW);
    float falloff = 1.0f;
    if (cosine < light.cosOpeningAngle)
    {
        falloff = 0.0f;
    }
    else if (light.penumbraAngle > 0.0f)
    {
        float deltaAngle = light.openingAngle - acos(clamp(cosine, -1.0f, 1.0f));
        falloff = smoothstep(0.0f, light.penumbraAngle, deltaAngle);
    }

    result.radiance = light.intensity * (falloff / distanceSquared);
    result.pdf = 0.0f;

    return true;
}

bool sampleDirectionalLight(float3 origin, LightData light, out AnalyticLightSample result)
{
    result = {};
    result.direction = -normalize(light.dirW);
    result.radiance = light.intensity;
    result.distance = kMaxRayDistance;
    result.pdf = 0.0;

    return true;
}

bool sampleRectAreaLight(float3 origin, LightData light, float2 uv, out AnalyticLightSample result)
{
    result = {};

    result.position = sampleRectLightPosition(light, uv);
    float3 offset = result.position - origin;
    float distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0)
        return false;

    result.distance = sqrt(distanceSquared);
    result.direction = offset / result.distance;
    float projectedArea = rectLightArea(light) * dot(light.dirW, -result.direction);
    if (projectedArea <= 0.0)
        return false;

    result.pdf = distanceSquared / projectedArea;
    result.radiance = light.intensity;

    return true;
}

bool sampleLight<S : ISampleGenerator>(float3 origin, LightData light, inout S generator,
    out AnalyticLightSample result)
{
    switch (light.type)
    {
    case LightType::Point:
        return samplePointLight(origin, light, result);
    case LightType::Directional:
        return sampleDirectionalLight(origin, light, result);
    case LightType::RectArea:
        return sampleRectAreaLight(origin, light, sampleNext2D(generator), result);
    default:
        result = {};

        return false;
    }
}

#endif
