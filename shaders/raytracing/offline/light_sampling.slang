#ifndef RENDERLAB_OFFLINE_LIGHT_SAMPLING_SLANG
#define RENDERLAB_OFFLINE_LIGHT_SAMPLING_SLANG

#include "raytracing/offline/light.slang"
#include "common/random.slang"

static const float kMaxRayDistance = 3.402823466e+38;

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
DeltaLightSample sampleLocalLight(uint pointCount, uint spotCount, float3 origin, inout uint state)
{
    DeltaLightSample selected = {};
    float selectedWeight = 0.0;
    float weightSum = 0.0;
    for (uint candidate = 0; candidate < pointCount + spotCount; ++candidate)
    {
        float3 position;
        LightEmission emission;
        if (candidate < pointCount)
        {
            PointLight light = pointLights[candidate];
            position = light.position;
            emission = light.emission;
        }
        else
        {
            SpotLight light = spotLights[candidate - pointCount];
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
            SpotLight light = spotLights[candidate - pointCount];
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

        float3 incident = emissionRgb(emission, float2(0.0)) * (attenuation / distanceSquared);
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
        DirectionalLight light = directionalLights[index];
        float3 incident = emissionRgb(light.emission, float2(0.0));
        float weight = lightWeight(incident);
        if (weight <= 0.0)
        {
            continue;
        }
        weightSum += weight;
        if (randomFloat(state) < weight / weightSum)
        {
            selected.direction = -light.direction;
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
    LightTriangle triangle = lightTriangles[lightIndex];
    float3 center = (triangle.p0 + triangle.p1.xyz + triangle.p2.xyz) / 3.0;
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
    float intensity = emission.hasImage
        ? emission.scale : lightWeight(emission.rgb * emission.scale);

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

float3 sampleTriangle(uint lightIndex, float2 sampleValue, out float2 uv)
{
    LightTriangle triangle = lightTriangles[lightIndex];
    float root = sqrt(sampleValue.x);
    float3 barycentrics = float3(1.0 - root, root * (1.0 - sampleValue.y), root * sampleValue.y);
    uv = triangle.uv01.xy * barycentrics.x + triangle.uv01.zw * barycentrics.y + triangle.uv2 * barycentrics.z;

    return triangle.p0 * barycentrics.x + triangle.p1.xyz * barycentrics.y + triangle.p2.xyz * barycentrics.z;
}

// Conditional PDF in solid angle; discrete triangle selection is applied by the caller.
float areaPdf(uint lightIndex, float3 origin, float3 target)
{
    LightTriangle triangle = lightTriangles[lightIndex];
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
