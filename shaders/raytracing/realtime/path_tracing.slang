#ifndef RENDERLAB_PATH_TRACING_SLANG
#define RENDERLAB_PATH_TRACING_SLANG

#include "render/raytracing/RayTracingBindings.h"

#include "common/light_data.slang"
#include "common/constant.slang"
#include "common/random.slang"
#include "common/local_light_sampling.slang"
#include "common/rect_area_light_sampling.slang"
#include "common/material_evaluation.slang"
#include "common/pbr_brdf.slang"
#include "common/normal_mapping.slang"
#include "common/scene_data.slang"
#include "raytracing/realtime/geometry.slang"
#include "raytracing/realtime/triangle_light.slang"

static const uint kRussianRouletteStartBounce = 3;
static const float kRayEpsilon = 0.001;
static const uint kVisibilityMissIndex = 1;
static const float kMaxRayDistance = 3.402823466e+38;
// Match the instance masks in TlasBuildPass.cpp.
static const uint kSceneMask = 0x01;
static const uint kLightMask = 0x02;

[[vk::binding(RealtimeRayTracingBinding::kTlas, 3)]] RaytracingAccelerationStructure scene;
[[vk::binding(1, 1)]] StructuredBuffer<LightData> lights;
[[vk::binding(8, 1)]] TextureCube<float4> radianceMap;
[[vk::binding(9, 1)]] SamplerState radianceSampler;
[[vk::binding(RealtimeRayTracingBinding::kTriangles, 3)]] ByteAddressBuffer emissiveTriangles;

EmissiveTriangle loadEmissiveTriangle(uint index)
{
    return emissiveTriangles.Load<EmissiveTriangle>(kEmissiveTriangleHeaderSize + index * kEmissiveTriangleStride);
}

struct VisibilityPayload
{
    uint visible;
};

float balanceHeuristic(float samplePdf, float otherPdf)
{
    return samplePdf / (samplePdf + otherPdf);
}

struct Surface
{
    float3 position;
    float3 geometricNormal;
    float3 normal;
    float3 viewDirection; // World-space direction toward the previous ray vertex (wo).
    PbrBrdf brdf;
    float3 emissive;
    uint emissiveTriangleIndex;

    float3 rayOrigin()
    {
        return position + geometricNormal * kRayEpsilon;
    }

    float3 evaluateLight(float3 direction, float3 incidentLight)
    {
        if (dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        return brdf.evaluate(normal, viewDirection, direction)
            * saturate(dot(normal, direction)) * incidentLight;
    }

    bool isVisible(float3 direction, float distance)
    {
        if (distance <= 2.0 * kRayEpsilon)
        {
            return true;
        }

        RayDesc ray;
        ray.Origin = rayOrigin();
        ray.Direction = direction;
        ray.TMin = kRayEpsilon;
        ray.TMax = distance - kRayEpsilon;

        VisibilityPayload payload = {0};
        TraceRay(
            scene,
            RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
                RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            kSceneMask | kLightMask, 0, 0, kVisibilityMissIndex, ray, payload
        );
        return payload.visible != 0;
    }

    float3 sampleLocalLightRadiance(LightRanges ranges, inout uint state)
    {
        LocalLightSample selected = sampleLocalLight(lights, ranges, position, state);
        if (selected.probability == 0.0)
        {
            return float3(0.0);
        }

        LightData light = lights[selected.index];
        float nDotL = saturate(dot(normal, selected.direction));
        if (nDotL == 0.0)
        {
            return float3(0.0);
        }

        float3 shadowOffset = light.position - rayOrigin();
        float shadowDistance = length(shadowOffset);
        if (!isVisible(normalize(shadowOffset), shadowDistance))
        {
            return float3(0.0);
        }

        // Delta point and spot lights do not compete with continuous BRDF direction sampling.
        return evaluateLight(selected.direction, selected.incidentIrradiance) / selected.probability;
    }

    float3 sampleRectAreaLightRadiance(RectAreaLightSample selected, inout uint state)
    {
        if (selected.probability == 0.0)
        {
            return float3(0.0);
        }

        float3 origin = rayOrigin();
        RectAreaLight emitter = {lights[selected.index]};
        float3 sampleLightPosition = emitter.samplePosition(state);
        float3 toLight = sampleLightPosition - origin;
        float distanceSquared = dot(toLight, toLight);
        if (distanceSquared == 0.0)
        {
            return float3(0.0);
        }

        float3 lightDirection = toLight * rsqrt(distanceSquared);
        float nDotL = saturate(dot(normal, lightDirection));
        float lightPdf = selected.probability * emitter.pdf(origin, sampleLightPosition);
        if (nDotL == 0.0 || lightPdf == 0.0 || !isVisible(lightDirection, sqrt(distanceSquared)))
        {
            return float3(0.0);
        }

        // The full NEE PDF includes discrete light selection and conditional surface sampling.
        float brdfPdf = dot(geometricNormal, lightDirection) > 0.0
            ? brdf.pdf(normal, viewDirection, lightDirection) : 0.0;
        float weight = balanceHeuristic(lightPdf, brdfPdf);
        return evaluateLight(lightDirection, emitter.data.color) * weight / lightPdf;
    }

    float3 sampleEmissiveTriangleRadiance(uint triangleCount, inout uint state)
    {
        if (triangleCount == 0)
        {
            return float3(0.0);
        }

        uint index = sampleEmissiveTriangleIndex(triangleCount, state);
        EmissiveTriangle light = loadEmissiveTriangle(index);
        float3 barycentrics = sampleTriangleBarycentrics(state);
        float3 target = light.position + light.edge1 * barycentrics.y + light.edge2 * barycentrics.z;
        float3 origin = rayOrigin();
        float lightPdf = triangleLightPdf(light, origin, target, triangleCount);
        if (lightPdf <= 0.0)
        {
            return float3(0.0);
        }
        float3 offset = target - origin;
        float distance = length(offset);
        float3 direction = offset / distance;
        if (dot(geometricNormal, direction) <= 0.0 || dot(normal, direction) <= 0.0)
        {
            return float3(0.0);
        }

        float3 emission = triangleLightEmission(light, barycentrics);
        if (!any(emission > 0.0) || !isVisible(direction, distance))
        {
            return float3(0.0);
        }
        float brdfPdf = brdf.pdf(normal, viewDirection, direction);
        float weight = balanceHeuristic(lightPdf, brdfPdf);
        return evaluateLight(direction, emission) * weight / lightPdf;
    }

    float3 sampleDirectRadiance(LightRanges ranges, uint triangleCount, inout uint state,
        out float rectAreaLightWeightSum)
    {
        float3 radiance = sampleLocalLightRadiance(ranges, state);
        for (uint index = 0; index < ranges.directionalEnd; ++index)
        {
            LightData light = lights[index];

            // Stored direction is light propagation; visibility points toward the source at infinity.
            float3 lightDirection = -normalize(light.direction.xyz);
            float nDotL = saturate(dot(normal, lightDirection));
            if (nDotL == 0.0 || !isVisible(lightDirection, kMaxRayDistance))
            {
                continue;
            }

            // A delta light has no competing BRDF-sampling path or distance attenuation.
            radiance += evaluateLight(lightDirection, light.color);
        }

        // NEE and the BRDF bounce share the selection distribution at the same ray origin.
        float3 origin = rayOrigin();
        RectAreaLightSample selected = sampleRectAreaLight(lights, ranges.pointEnd, ranges.rectAreaEnd, origin, state);
        radiance += sampleRectAreaLightRadiance(selected, state);
        radiance += sampleEmissiveTriangleRadiance(triangleCount, state);

        rectAreaLightWeightSum = selected.weightSum;
        return radiance;
    }
};

Surface surfaceAt(RayTracingHitData hit, uint primitiveIndex, float2 barycentrics)
{
    uint* indices = (uint*)hit.indexAddress;
    uint first = hit.firstIndex + primitiveIndex * 3;
    VertexAttributes v0 = hit.loadVertex(indices[first]);
    VertexAttributes v1 = hit.loadVertex(indices[first + 1]);
    VertexAttributes v2 = hit.loadVertex(indices[first + 2]);
    float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics);
    float2 uv = v0.uv * weights.x + v1.uv * weights.y + v2.uv * weights.z;
    float3 objectNormal = v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z;
    float4 objectTangent = v0.tangent * weights.x + v1.tangent * weights.y + v2.tangent * weights.z;
    MaterialData material = materials[hit.materialIndex];

    // Row-vector multiplication applies the inverse transpose, including nonuniform scale.
    float3 worldNormal = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));
    float3x3 model = (float3x3)ObjectToWorld3x4();
    float handedness = determinant(model) < 0.0 ? -1.0 : 1.0;
    float4 worldTangent = float4(mul(model, objectTangent.xyz), sign(objectTangent.w) * handedness);
    float3 geometricNormal = normalize(cross(mul(model, v1.position - v0.position),
        mul(model, v2.position - v0.position)));
    float3 emissionNormal = geometricNormal;
    if (dot(geometricNormal, worldNormal) < 0.0)
    {
        geometricNormal = -geometricNormal;
    }

    Surface surface;
    surface.position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    surface.viewDirection = -normalize(WorldRayDirection());
    surface.normal = applyNormalMap(worldNormal, worldTangent,
        sampleTextureLevel(material.normalTexture, uv).xyz, material.normalScale);
    float facing = dot(geometricNormal, surface.viewDirection) < 0.0 ? -1.0 : 1.0;
    surface.geometricNormal = geometricNormal * facing;
    surface.normal *= facing;
    if (dot(surface.normal, surface.viewDirection) <= 0.0)
    {
        surface.normal = surface.geometricNormal;
    }

    surface.brdf = evaluatePbrMaterial(material, uv, RayMaterialSampler()).brdf;
    surface.emissive = sampleTextureLevel(material.emissiveTexture, uv).rgb * material.emission;
    if (material.doubleSided == 0 && dot(emissionNormal, surface.viewDirection) <= 0.0)
    {
        surface.emissive = float3(0.0);
    }
    surface.emissiveTriangleIndex = hit.firstEmissiveTriangle == kInvalidEmissiveTriangle
        ? kInvalidEmissiveTriangle : hit.firstEmissiveTriangle + primitiveIndex;
    return surface;
}

struct PathTracingParameters
{
    float4 viewport;
    LightRanges lightRanges;
    uint maxBounces;
    uint sampleIndex;
    uint iblEnabled;
    uint padding;

    property float2 viewportOffset { get { return viewport.xy; } }
    property float2 viewportExtent { get { return viewport.zw; } }

    bool containsPixel(uint2 pixel)
    {
        float2 local = float2(pixel) + 0.5 - viewportOffset;
        return all(local >= 0.0) && all(local < viewportExtent);
    }

    RayDesc createRay(uint2 pixel, float2 pixelSample)
    {
        float2 local = float2(pixel) + pixelSample - viewportOffset;
        float2 ndc = local / viewportExtent * 2.0 - 1.0;
        float4 farPoint = mul(sceneUniforms.inverseViewProjection, float4(ndc, 1.0, 1.0));
        float3 toFar = farPoint.xyz / farPoint.w - sceneUniforms.cameraPosition.xyz;

        RayDesc ray;
        ray.Origin = sceneUniforms.cameraPosition.xyz;
        ray.Direction = normalize(toFar);
        ray.TMin = kRayEpsilon;
        ray.TMax = kMaxRayDistance;
        return ray;
    }
};

[[vk::push_constant]] ConstantBuffer<PathTracingParameters> parameters;
[[vk::binding(RealtimeRayTracingBinding::kObjects, 3)]] StructuredBuffer<RayTracingHitData> objects;

enum class PathEvent : uint
{
    Miss,
    Surface,
    Emitter,
};

// Hit shaders only return a result. All path and visibility rays originate in RayGen.
struct PathHit
{
    PathEvent event;
    uint lightIndex;
    float distance;
    Surface surface;
};

struct PathResult
{
    float3 directRadiance;
    float3 indirectRadiance;
    float3 emission;
    float primaryDepth;
    float3 primaryNormal;
    float3 primaryAlbedo;

    [mutating]
    void addRadiance(float3 radiance, bool direct)
    {
        if (direct)
        {
            directRadiance += radiance;
        }
        else
        {
            indirectRadiance += radiance;
        }
    }

    [mutating]
    void addEmission(float3 radiance, uint bounce)
    {
        if (bounce == 0)
        {
            emission += radiance;
        }
        else
        {
            addRadiance(radiance, bounce == 1);
        }
    }
};

PathResult tracePath(RayDesc ray, uint primaryMask, uint primaryFlags, inout uint state)
{
    PathResult result = {};
    result.primaryDepth = -1.0;
    result.primaryAlbedo = float3(1.0);
    float3 throughput = float3(1.0);
    float previousBrdfPdf = 0.0;
    float previousRectAreaWeightSum = 0.0;
    uint triangleCount = emissiveTriangles.Load(0);

    for (uint bounce = 0; bounce <= parameters.maxBounces; ++bounce)
    {
        PathHit hit = {};
        uint mask = bounce == 0 ? primaryMask : kSceneMask | kLightMask;
        uint flags = bounce == 0 ? primaryFlags : RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
        TraceRay(scene, flags, mask, 0, 0, 0, ray, hit);

        if (hit.event == PathEvent::Miss)
        {
            if (parameters.iblEnabled != 0)
            {
                result.addEmission(throughput * radianceMap.SampleLevel(radianceSampler, ray.Direction, 0.0).rgb,
                    bounce);
            }
            break;
        }
        if (hit.event == PathEvent::Emitter)
        {
            RectAreaLight emitter = {lights[hit.lightIndex]};
            float3 target = ray.Origin + ray.Direction * hit.distance;
            float lightPdf = emitter.pdf(ray.Origin, target);
            if (lightPdf > 0.0)
            {
                float weight = 1.0;
                if (bounce != 0)
                {
                    float selectionProbability = previousRectAreaWeightSum > 0.0
                        ? emitter.selectionWeight(ray.Origin) / previousRectAreaWeightSum : 0.0;
                    weight = balanceHeuristic(previousBrdfPdf, lightPdf * selectionProbability);
                }
                result.addEmission(throughput * emitter.data.color * weight, bounce);
            }
            break;
        }

        Surface surface = hit.surface;
        if (bounce == 0)
        {
            result.primaryDepth = linearViewDepth(surface.position, sceneUniforms.view);
            result.primaryNormal = surface.geometricNormal;
            result.primaryAlbedo = surface.brdf.albedo();
        }
        // Emission reached after one BRDF sample completes a direct-light path, including its MIS pair.
        float emissionWeight = 1.0;
        if (bounce != 0 && surface.emissiveTriangleIndex != kInvalidEmissiveTriangle)
        {
            EmissiveTriangle light = loadEmissiveTriangle(surface.emissiveTriangleIndex);
            float lightPdf = triangleLightPdf(light, ray.Origin, surface.position, triangleCount);
            emissionWeight = balanceHeuristic(previousBrdfPdf, lightPdf);
        }
        result.addEmission(throughput * surface.emissive * emissionWeight, bounce);
        // After the last scattering event, retain terminal emission without another visibility query.
        if (bounce == parameters.maxBounces)
        {
            break;
        }

        float rectAreaWeightSum;
        result.addRadiance(throughput * surface.sampleDirectRadiance(parameters.lightRanges,
            triangleCount, state, rectAreaWeightSum),
            bounce == 0);
        float3 randomSample;
        randomSample.x = randomFloat(state);
        randomSample.y = randomFloat(state);
        randomSample.z = randomFloat(state);
        BrdfSample sampled = surface.brdf.sample(surface.normal, surface.viewDirection, randomSample);
        if (sampled.pdf <= 0.0 || dot(surface.geometricNormal, sampled.direction) <= 0.0 || !any(sampled.weight > 0.0))
        {
            break;
        }

        throughput *= sampled.weight;
        if (bounce + 1 >= kRussianRouletteStartBounce)
        {
            float survivalProbability = saturate(max(throughput.x, max(throughput.y, throughput.z)));
            if (survivalProbability == 0.0)
            {
                break;
            }
            if (survivalProbability < 1.0 && randomFloat(state) >= survivalProbability)
            {
                break;
            }
            throughput /= survivalProbability;
        }

        previousBrdfPdf = sampled.pdf;
        previousRectAreaWeightSum = rectAreaWeightSum;
        ray.Origin = surface.rayOrigin();
        ray.Direction = sampled.direction;
        ray.TMin = kRayEpsilon;
        ray.TMax = kMaxRayDistance;
    }
    return result;
}

[shader("miss")]
void missMain(inout PathHit hit)
{
    hit.event = PathEvent::Miss;
}

[shader("miss")]
void visibilityMissMain(inout VisibilityPayload payload)
{
    payload.visible = 1;
}

[shader("closesthit")]
void closestHitMain(inout PathHit payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.event = PathEvent::Surface;
    RayTracingHitData hit = objects[InstanceID()];
    payload.surface = surfaceAt(hit, PrimitiveIndex(), attributes.barycentrics);
}

[shader("closesthit")]
void lightClosestHitMain(inout PathHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    hit.event = PathEvent::Emitter;
    hit.lightIndex = InstanceID();
    hit.distance = RayTCurrent();
}

#endif
