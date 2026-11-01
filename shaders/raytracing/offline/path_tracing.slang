#include "common/scene_data.slang"
#include "raytracing/offline/material.slang"
#include "raytracing/offline/brdf.slang"
#include "raytracing/offline/light_sampling.slang"
#include "common/low_discrepancy.slang"
#include "common/random.slang"

static const float kRayEpsilon = 0.001;
static const uint kSceneMask = 0x01;
static const uint kVisibilityMissIndex = 1;

struct HitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint4 material; // First index, material index, reverse orientation, first area light.
};
struct PathParameters
{
    float4 viewport;
    uint4 samples; // Accumulated samples, max bounces, reserved, reserved.
    uint4 lightCounts;
    uint4 mappedLightCounts;
};
struct Hit
{
    uint hit;
    uint objectIndex;
    uint primitiveIndex;
    float2 uv;
    float3 position;
    float3 normal;
    float3 geometricNormal;
};
struct Visibility
{
    uint visible;
};
struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

[[vk::push_constant]] ConstantBuffer<PathParameters> parameters;
[[vk::binding(OfflineRayTracingBinding::kTlas, 3)]] RaytracingAccelerationStructure scene;
[[vk::binding(OfflineRayTracingBinding::kObjects, 3)]] StructuredBuffer<HitData> objects;
[[vk::binding(OfflineRayTracingBinding::kColor, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(OfflineRayTracingBinding::kAccumulation, 3)]] RWStructuredBuffer<float4> accumulation;

float balanceHeuristic(float pdf, float otherPdf)
{
    return pdf / (pdf + otherPdf);
}

bool visible(float3 origin, float3 direction, float distance)
{
    if (distance <= kRayEpsilon)
    {
        return false;
    }
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = distance - kRayEpsilon;
    Visibility visibility = {};
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, kSceneMask, 0, 0, kVisibilityMissIndex, ray, visibility);
    return visibility.visible != 0;
}

struct Surface
{
    float3 origin;
    float3 normal;
    float3 geometricNormal;
    DiffuseBrdf brdf;

    float3 evaluate(float3 direction, float3 incident)
    {
        if (dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        return brdf.evaluate() * max(0.0, dot(normal, direction)) * incident;
    }

    float3 deltaRadiance(DeltaLightSample selected)
    {
        if (selected.probability == 0.0 || dot(normal, selected.direction) <= 0.0 ||
            dot(geometricNormal, selected.direction) <= 0.0)
        {
            return float3(0.0);
        }
        if (!visible(origin, selected.direction, selected.distance))
        {
            return float3(0.0);
        }
        return evaluate(selected.direction, selected.incidentIrradiance) / selected.probability;
    }

    float3 areaRadiance(AreaLightSample selected, float2 sampleValue)
    {
        if (selected.probability == 0.0)
        {
            return float3(0.0);
        }
        float2 uv;
        float3 target = sampleTriangle(selected.index, sampleValue, uv);
        float3 offset = target - origin;
        float distance = length(offset);
        if (distance <= kRayEpsilon)
        {
            return float3(0.0);
        }
        float3 direction = offset / distance;
        float lightPdf = selected.probability * areaPdf(selected.index, origin, target);
        if (lightPdf == 0.0 || dot(normal, direction) <= 0.0 || dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        if (!visible(origin, direction, distance))
        {
            return float3(0.0);
        }
        float weight = balanceHeuristic(lightPdf, brdf.pdf(normal, direction));
        float3 emission = areaEmission(selected.index, uv, -direction);
        return evaluate(direction, emission) * (weight / lightPdf);
    }
};

float3 tracePath(RayDesc ray, inout uint state)
{
    float3 radiance = float3(0.0);
    float3 throughput = float3(1.0);
    float previousBrdfPdf = 0.0;
    float previousAreaWeightSum = 0.0;
    uint previousDelta = 0;
    for (uint bounce = 0; bounce <= parameters.samples.y; ++bounce)
    {
        Hit hit = {};
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, kSceneMask, 0, 0, 0, ray, hit);
        if (hit.hit == 0)
        {
            radiance += throughput * environmentEmission(parameters.mappedLightCounts.x, ray.Direction);
            break;
        }

        HitData object = objects[hit.objectIndex];
        if (object.material.w != kOfflineInvalidIndex)
        {
            uint lightIndex = object.material.w + hit.primitiveIndex;
            float weight = 1.0;
            if (bounce != 0 && previousDelta == 0)
            {
                float probability = previousAreaWeightSum > 0.0
                    ? areaSelectionWeight(lightIndex, ray.Origin) / previousAreaWeightSum : 0.0;
                float lightPdf = probability * areaPdf(lightIndex, ray.Origin, hit.position);
                weight = balanceHeuristic(previousBrdfPdf, lightPdf);
            }
            radiance += throughput * areaEmission(lightIndex, hit.uv, -ray.Direction) * weight;
        }
        if (bounce == parameters.samples.y)
        {
            break;
        }

        float3 wo = -ray.Direction;
        MaterialPreview header = materialInstructions[object.material.y];
        BrdfSample sampled;
        sampled.direction = float3(0.0);
        sampled.weight = float3(0.0);
        sampled.pdf = 0.0;
        sampled.delta = 0;
        float3 nextOrigin = hit.position;
        float areaWeightSum = 0.0;

        if (header.model == OfflineMaterialPreviewModel::SmoothDielectric)
        {
            float3 geometricNormal = hit.geometricNormal;
            float3 shadingNormal = hit.normal;
            if (dot(shadingNormal, geometricNormal) < 0.0)
            {
                shadingNormal = -shadingNormal;
            }

            float3 facingNormal = dot(geometricNormal, wo) >= 0.0 ? geometricNormal : -geometricNormal;
            if (dot(shadingNormal, wo) <= 0.0)
            {
                shadingNormal = facingNormal;
            }
            else if (dot(shadingNormal, facingNormal) < 0.0)
            {
                shadingNormal = -shadingNormal;
            }

            float3 etaRgb = evaluateTexture(header.firstTexture, hit.uv, hit.position, hit.normal, hit.primitiveIndex);
            float materialEta = max((etaRgb.x + etaRgb.y + etaRgb.z) / 3.0, 1e-4);
            float relativeEta = dot(geometricNormal, wo) >= 0.0 ? materialEta : 1.0 / materialEta;

            SmoothDielectricBsdf bsdf;
            bsdf.eta = relativeEta;
            sampled = bsdf.sample(wo, shadingNormal, randomFloat(state));
            if (!any(sampled.direction != 0.0))
            {
                break;
            }

            bool transmitted = dot(sampled.direction, facingNormal) < 0.0;
            nextOrigin = hit.position + facingNormal * (transmitted ? -kRayEpsilon : kRayEpsilon);
        }
        else
        {
            Surface surface;
            surface.geometricNormal = dot(hit.geometricNormal, wo) >= 0.0 ? hit.geometricNormal : -hit.geometricNormal;
            surface.normal = dot(hit.normal, surface.geometricNormal) >= 0.0 ? hit.normal : -hit.normal;
            if (dot(surface.normal, wo) <= 0.0)
            {
                surface.normal = surface.geometricNormal;
            }
            surface.origin = hit.position + surface.geometricNormal * kRayEpsilon;
            surface.brdf.albedo = saturate(evaluateMaterial(object.material.y, hit.uv, hit.position, hit.normal,
                hit.primitiveIndex));
            if (!any(surface.brdf.albedo > 0.0))
            {
                break;
            }

            DeltaLightSample local = sampleLocalLight(parameters.lightCounts.x, parameters.lightCounts.y,
                surface.origin, state);
            DeltaLightSample distant = sampleDistantLight(parameters.lightCounts.z, state);
            AreaLightSample area = sampleAreaLight(parameters.lightCounts.w, surface.origin, state);
            float2 areaSample;
            areaSample.x = randomFloat(state);
            areaSample.y = randomFloat(state);
            radiance += throughput * (surface.deltaRadiance(local) + surface.deltaRadiance(distant) +
                surface.areaRadiance(area, areaSample));
            areaWeightSum = area.weightSum;

            float2 brdfSample;
            brdfSample.x = randomFloat(state);
            brdfSample.y = randomFloat(state);
            sampled = surface.brdf.sample(surface.normal, brdfSample);
            if (sampled.pdf == 0.0 || dot(surface.geometricNormal, sampled.direction) <= 0.0)
            {
                break;
            }
            nextOrigin = surface.origin;
        }

        throughput *= sampled.weight;
        previousBrdfPdf = sampled.pdf;
        previousDelta = sampled.delta;
        previousAreaWeightSum = areaWeightSum;
        ray.Origin = nextOrigin;
        ray.Direction = sampled.direction;
        ray.TMin = 0.0;
        ray.TMax = kMaxRayDistance;
    }
    return radiance;
}

Vertex loadVertex(HitData object, uint index)
{
    float* vertex = (float*)object.vertexAddress + index * 12;
    return {float3(vertex[0], vertex[1], vertex[2]), float3(vertex[3], vertex[4], vertex[5]),
        float2(vertex[6], vertex[7])};
}

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint pixelIndex = pixel.x + pixel.y * DispatchRaysDimensions().x;
    float2 localPixel = float2(pixel) + 0.5 - parameters.viewport.xy;
    if (any(localPixel < 0.0) || any(localPixel >= parameters.viewport.zw))
    {
        accumulation[pixelIndex] = float4(0.0);
        outputImage[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // 低差异化序列+随机数 作为像素内抖动采样点
    uint scrambleState = pixelIndex;
    uint2 scrambleSeeds;
    scrambleSeeds.x = randomUint(scrambleState);
    scrambleSeeds.y = randomUint(scrambleState);
    float2 pixelSample = sampleSobol2D(parameters.samples.x, scrambleSeeds);
    float2 ndc = (float2(pixel) + pixelSample - parameters.viewport.xy) / parameters.viewport.zw * 2.0 - 1.0;
    float4 farPoint = mul(sceneUniforms.inverseViewProjection, float4(ndc, 1.0, 1.0));
    RayDesc ray;
    ray.Origin = sceneUniforms.cameraPosition.xyz;
    ray.Direction = normalize(farPoint.xyz / farPoint.w - ray.Origin);
    ray.TMin = kRayEpsilon;
    ray.TMax = kMaxRayDistance;

    uint sampleState = parameters.samples.x;
    uint pathState = scrambleSeeds.y ^ randomUint(sampleState);
    float3 color = tracePath(ray, pathState);

    float3 sum = color;

    if (parameters.samples.x != 0)
    {
        sum += accumulation[pixelIndex].rgb;
    }

    accumulation[pixelIndex] = float4(sum, 0.0);
    outputImage[pixel] = float4(sum / (float(parameters.samples.x) + 1.0), 1.0);
}

[shader("miss")]
void missMain(inout Hit hit) { hit.hit = 0; }

[shader("miss")]
void visibilityMissMain(inout Visibility visibility) { visibility.visible = 1; }

[shader("closesthit")]
void closestHitMain(inout Hit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    HitData object = objects[InstanceID()];
    uint* indices = (uint*)object.indexAddress;
    uint first = object.material.x + PrimitiveIndex() * 3;
    Vertex v0 = loadVertex(object, indices[first]);
    Vertex v1 = loadVertex(object, indices[first + 1]);
    Vertex v2 = loadVertex(object, indices[first + 2]);
    float3 weights = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
    float3 objectNormal = v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z;
    hit.hit = 1;
    hit.objectIndex = InstanceID();
    hit.primitiveIndex = PrimitiveIndex();
    hit.position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    hit.uv = v0.uv * weights.x + v1.uv * weights.y + v2.uv * weights.z;
    hit.normal = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));
    float3 objectGeometricNormal = cross(v1.position - v0.position, v2.position - v0.position);
    hit.geometricNormal = normalize(mul(objectGeometricNormal, (float3x3)WorldToObject3x4()));
    if (object.material.z != 0)
    {
        hit.normal = -hit.normal;
        hit.geometricNormal = -hit.geometricNormal;
    }
}
