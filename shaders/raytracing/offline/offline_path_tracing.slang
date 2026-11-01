#ifndef RENDERLAB_OFFLINE_PATH_TRACING_SLANG
#define RENDERLAB_OFFLINE_PATH_TRACING_SLANG

#include "common/scene_data.slang"
#include "raytracing/offline/offline_material.slang"
#include "raytracing/offline/offline_brdf.slang"
#include "raytracing/offline/offline_light_sampling.slang"

static const float kOfflineRayEpsilon = 0.001;
static const uint kOfflineSceneMask = 0x01;
static const uint kOfflineVisibilityMissIndex = 1;

struct OfflineHitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint4 material; // First index, material index, reverse orientation, first area light.
};
struct OfflinePathParameters
{
    float4 viewport;
    uint4 samples; // Accumulated samples, max bounces, reserved, reserved.
    uint4 lightCounts;
    uint4 mappedLightCounts;
};
struct OfflineHit
{
    uint hit;
    uint objectIndex;
    uint primitiveIndex;
    float2 uv;
    float3 position;
    float3 normal;
    float3 geometricNormal;
};
struct OfflineVisibility
{
    uint visible;
};

[[vk::push_constant]] ConstantBuffer<OfflinePathParameters> parameters;
[[vk::binding(OfflineRayTracingBinding::kTlas, 3)]] RaytracingAccelerationStructure scene;
[[vk::binding(OfflineRayTracingBinding::kObjects, 3)]] StructuredBuffer<OfflineHitData> objects;

float offlineBalanceHeuristic(float pdf, float otherPdf)
{
    return pdf / (pdf + otherPdf);
}

bool offlineVisible(float3 origin, float3 direction, float distance)
{
    if (distance <= kOfflineRayEpsilon)
    {
        return false;
    }
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = distance - kOfflineRayEpsilon;
    OfflineVisibility visibility = {};
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, kOfflineSceneMask, 0, 0, kOfflineVisibilityMissIndex, ray, visibility);
    return visibility.visible != 0;
}

struct OfflineSurface
{
    float3 origin;
    float3 normal;
    float3 geometricNormal;
    OfflineDiffuseBrdf brdf;

    float3 evaluate(float3 direction, float3 incident)
    {
        if (dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        return brdf.evaluate() * max(0.0, dot(normal, direction)) * incident;
    }

    float3 deltaRadiance(OfflineDeltaLightSample selected)
    {
        if (selected.probability == 0.0 || dot(normal, selected.direction) <= 0.0 ||
            dot(geometricNormal, selected.direction) <= 0.0)
        {
            return float3(0.0);
        }
        if (!offlineVisible(origin, selected.direction, selected.distance))
        {
            return float3(0.0);
        }
        return evaluate(selected.direction, selected.incidentIrradiance) / selected.probability;
    }

    float3 areaRadiance(OfflineAreaLightSample selected, float2 sampleValue)
    {
        if (selected.probability == 0.0)
        {
            return float3(0.0);
        }
        float2 uv;
        float3 target = offlineSampleTriangle(selected.index, sampleValue, uv);
        float3 offset = target - origin;
        float distance = length(offset);
        if (distance <= kOfflineRayEpsilon)
        {
            return float3(0.0);
        }
        float3 direction = offset / distance;
        float lightPdf = selected.probability * offlineAreaPdf(selected.index, origin, target);
        if (lightPdf == 0.0 || dot(normal, direction) <= 0.0 || dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        if (!offlineVisible(origin, direction, distance))
        {
            return float3(0.0);
        }
        float weight = offlineBalanceHeuristic(lightPdf, brdf.pdf(normal, direction));
        float3 emission = offlineAreaEmission(selected.index, uv, -direction);
        return evaluate(direction, emission) * (weight / lightPdf);
    }
};

float3 traceOfflinePath(RayDesc ray, inout uint state)
{
    float3 radiance = float3(0.0);
    float3 throughput = float3(1.0);
    float previousBrdfPdf = 0.0;
    float previousAreaWeightSum = 0.0;
    for (uint bounce = 0; bounce <= parameters.samples.y; ++bounce)
    {
        OfflineHit hit = {};
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, kOfflineSceneMask, 0, 0, 0, ray, hit);
        if (hit.hit == 0)
        {
            radiance += throughput * offlineEnvironmentEmission(parameters.mappedLightCounts.x, ray.Direction);
            break;
        }

        OfflineHitData object = objects[hit.objectIndex];
        if (object.material.w != kOfflineInvalidIndex)
        {
            uint lightIndex = object.material.w + hit.primitiveIndex;
            float weight = 1.0;
            if (bounce != 0)
            {
                float probability = previousAreaWeightSum > 0.0
                    ? offlineAreaSelectionWeight(lightIndex, ray.Origin) / previousAreaWeightSum : 0.0;
                float lightPdf = probability * offlineAreaPdf(lightIndex, ray.Origin, hit.position);
                weight = offlineBalanceHeuristic(previousBrdfPdf, lightPdf);
            }
            radiance += throughput * offlineAreaEmission(lightIndex, hit.uv, -ray.Direction) * weight;
        }
        if (bounce == parameters.samples.y)
        {
            break;
        }

        OfflineSurface surface;
        surface.geometricNormal = dot(hit.geometricNormal, -ray.Direction) >= 0.0
            ? hit.geometricNormal : -hit.geometricNormal;
        surface.normal = dot(hit.normal, surface.geometricNormal) >= 0.0 ? hit.normal : -hit.normal;
        if (dot(surface.normal, -ray.Direction) <= 0.0)
        {
            surface.normal = surface.geometricNormal;
        }
        surface.origin = hit.position + surface.geometricNormal * kOfflineRayEpsilon;
        surface.brdf.albedo = saturate(evaluateMaterial(object.material.y, hit.uv, hit.position, hit.normal,
            hit.primitiveIndex));
        if (!any(surface.brdf.albedo > 0.0))
        {
            break;
        }

        OfflineDeltaLightSample local = sampleOfflineLocalLight(parameters.lightCounts.x, parameters.lightCounts.y,
            surface.origin, state);
        OfflineDeltaLightSample distant = sampleOfflineDistantLight(parameters.lightCounts.z, state);
        OfflineAreaLightSample area = sampleOfflineAreaLight(parameters.lightCounts.w, surface.origin, state);
        float2 areaSample;
        areaSample.x = randomFloat(state);
        areaSample.y = randomFloat(state);
        radiance += throughput * (surface.deltaRadiance(local) + surface.deltaRadiance(distant) +
            surface.areaRadiance(area, areaSample));

        float2 brdfSample;
        brdfSample.x = randomFloat(state);
        brdfSample.y = randomFloat(state);
        OfflineBrdfSample sampled = surface.brdf.sample(surface.normal, brdfSample);
        if (sampled.pdf == 0.0 || dot(surface.geometricNormal, sampled.direction) <= 0.0)
        {
            break;
        }
        throughput *= sampled.weight;
        previousBrdfPdf = sampled.pdf;
        previousAreaWeightSum = area.weightSum;
        ray.Origin = surface.origin;
        ray.Direction = sampled.direction;
        ray.TMin = 0.0;
        ray.TMax = kOfflineMaxRayDistance;
    }
    return radiance;
}

#endif
