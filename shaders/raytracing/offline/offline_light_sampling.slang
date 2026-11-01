#ifndef RENDERLAB_OFFLINE_LIGHT_SAMPLING_SLANG
#define RENDERLAB_OFFLINE_LIGHT_SAMPLING_SLANG

#include "raytracing/offline/offline_light.slang"
#include "common/random.slang"

static const float kOfflineMaxRayDistance = 3.402823466e+38;

float offlineLightWeight(float3 value)
{
    return max(0.0, max(value.x, max(value.y, value.z)));
}

struct OfflineDeltaLightSample
{
    float3 direction;
    float3 incidentIrradiance;
    float distance;
    float probability;
};

// Point and spot lights share one weighted reservoir. Their emission is intensity I.
OfflineDeltaLightSample sampleOfflineLocalLight(uint pointCount, uint spotCount, float3 origin, inout uint state)
{
    OfflineDeltaLightSample selected = {};
    float selectedWeight = 0.0;
    float weightSum = 0.0;
    for (uint candidate = 0; candidate < pointCount + spotCount; ++candidate)
    {
        float3 position;
        OfflineLightEmission emission;
        if (candidate < pointCount)
        {
            OfflinePointLight light = pointLights[candidate];
            position = light.position;
            emission = light.emission;
        }
        else
        {
            OfflineSpotLight light = spotLights[candidate - pointCount];
            position = light.position;
            emission = light.emission;
        }

        float3 offset = position - origin;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0)
        {
            continue;
        }
        float distance = sqrt(distanceSquared);
        float3 direction = offset / distance;
        float attenuation = 1.0;
        if (candidate >= pointCount)
        {
            OfflineSpotLight light = spotLights[candidate - pointCount];
            float cosine = dot(light.direction, -direction);
            if (light.cosInner <= light.cosOuter)
            {
                attenuation = cosine >= light.cosOuter ? 1.0 : 0.0;
            }
            else
            {
                float falloff = saturate((cosine - light.cosOuter) / (light.cosInner - light.cosOuter));
                attenuation = falloff * falloff * falloff * falloff;
            }
        }

        float3 incident = offlineEmissionRgb(emission, float2(0.0)) * (attenuation / distanceSquared);
        float weight = offlineLightWeight(incident);
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

OfflineDeltaLightSample sampleOfflineDistantLight(uint lightCount, inout uint state)
{
    OfflineDeltaLightSample selected = {};
    float selectedWeight = 0.0;
    float weightSum = 0.0;
    for (uint index = 0; index < lightCount; ++index)
    {
        OfflineDirectionalLight light = directionalLights[index];
        float3 incident = offlineEmissionRgb(light.emission, float2(0.0));
        float weight = offlineLightWeight(incident);
        if (weight <= 0.0)
        {
            continue;
        }
        weightSum += weight;
        if (randomFloat(state) < weight / weightSum)
        {
            selected.direction = -light.direction;
            selected.incidentIrradiance = incident;
            selected.distance = kOfflineMaxRayDistance;
            selectedWeight = weight;
        }
    }
    if (selectedWeight > 0.0)
    {
        selected.probability = selectedWeight / weightSum;
    }
    return selected;
}

float offlineAreaSelectionWeight(uint lightIndex, float3 origin)
{
    OfflineLightTriangle triangle = lightTriangles[lightIndex];
    float3 center = (triangle.p0Area.xyz + triangle.p1.xyz + triangle.p2.xyz) / 3.0;
    float3 offset = origin - center;
    float distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0 || triangle.p0Area.w == 0.0)
    {
        return 0.0;
    }
    float cosine = dot(triangle.normal.xyz, offset * rsqrt(distanceSquared));
    float projectedCosine = triangle.twoSided != 0 ? abs(cosine) : max(0.0, cosine);
    OfflineLightEmission emission = areaEmissions[triangle.emissionIndex];
    // A texture can be bright away from its centroid. Keep support over the whole triangle.
    float intensity = (emission.resources.w & kOfflineLightHasImage) != 0
        ? emission.colorScale.w : offlineLightWeight(emission.colorScale.rgb * emission.colorScale.w);
    return triangle.p0Area.w * projectedCosine * intensity / distanceSquared;
}

struct OfflineAreaLightSample
{
    uint index;
    float probability;
    float weightSum;
};

OfflineAreaLightSample sampleOfflineAreaLight(uint lightCount, float3 origin, inout uint state)
{
    OfflineAreaLightSample selected = {};
    float selectedWeight = 0.0;
    for (uint index = 0; index < lightCount; ++index)
    {
        float weight = offlineAreaSelectionWeight(index, origin);
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

float3 offlineSampleTriangle(uint lightIndex, float2 sampleValue, out float2 uv)
{
    OfflineLightTriangle triangle = lightTriangles[lightIndex];
    float root = sqrt(sampleValue.x);
    float3 barycentrics = float3(1.0 - root, root * (1.0 - sampleValue.y), root * sampleValue.y);
    uv = triangle.uv01.xy * barycentrics.x + triangle.uv01.zw * barycentrics.y + triangle.uv2 * barycentrics.z;
    return triangle.p0Area.xyz * barycentrics.x + triangle.p1.xyz * barycentrics.y + triangle.p2.xyz * barycentrics.z;
}

// Conditional PDF in solid angle; discrete triangle selection is applied by the caller.
float offlineAreaPdf(uint lightIndex, float3 origin, float3 target)
{
    OfflineLightTriangle triangle = lightTriangles[lightIndex];
    float3 offset = target - origin;
    float distanceSquared = dot(offset, offset);
    if (triangle.p0Area.w == 0.0 || distanceSquared == 0.0)
    {
        return 0.0;
    }
    float cosine = dot(triangle.normal.xyz, -offset * rsqrt(distanceSquared));
    float projectedCosine = triangle.twoSided != 0 ? abs(cosine) : max(0.0, cosine);
    if (projectedCosine == 0.0)
    {
        return 0.0;
    }
    return distanceSquared / (triangle.p0Area.w * projectedCosine);
}

#endif
