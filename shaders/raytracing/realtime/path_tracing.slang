#ifndef RENDERLAB_PATH_TRACING_SLANG
#define RENDERLAB_PATH_TRACING_SLANG

#include "render/raytracing/RayTracingBindings.h"

#include "common/light_data.slang"
#include "common/constant.slang"
#include "common/random.slang"
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

struct Surface
{
    float3 position;
    float3 geometricNormal;
    float3 normal;
    float3 viewDirection; // World-space direction toward the previous ray vertex (wo).
    PbrBrdf brdf;
    float3 emissive;
    uint emissiveTriangleIndex;
    uint materialIndex;
    uint objectIndex;

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
        sampleTextureLevel(material.normalTexture, uv).xyz, material.normalScale, material.normalEncoding);
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
    surface.materialIndex = hit.materialIndex;
    surface.objectIndex = InstanceID();
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
    uint candidateCount;

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
    float2 barycentrics;
};

struct PathResult
{
    float3 indirectRadiance;
    float3 emission;
    float primaryDepth;
    float3 primaryNormal;
    float3 primaryAlbedo;
};

#include "raytracing/realtime/restir.slang"

PathResult tracePath(RayDesc ray, uint primaryMask, uint primaryFlags, inout uint state,
    out RestirSurfaceData primarySurface, out RestirReservoir primaryReservoir)
{
    PathResult result = {};
    result.primaryDepth = -1.0;
    result.primaryAlbedo = float3(1.0);
    primarySurface = {};
    primaryReservoir = {};
    float3 throughput = float3(1.0);

    for (uint bounce = 0; bounce < parameters.maxBounces; ++bounce)
    {
        PathHit hit = {};
        uint mask = bounce == 0 ? primaryMask : kSceneMask | kLightMask;
        uint flags = bounce == 0 ? primaryFlags : RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
        TraceRay(scene, flags, mask, 0, 0, 0, ray, hit);

        if (hit.event == PathEvent::Miss)
        {
            if (bounce == 0 && parameters.iblEnabled != 0)
            {
                result.emission = radianceMap.SampleLevel(radianceSampler, ray.Direction, 0.0).rgb;
            }
            break;
        }
        if (hit.event == PathEvent::Emitter)
        {
            if (bounce == 0 && dot(lights[hit.lightIndex].direction.xyz, -ray.Direction) > 0.0)
            {
                result.emission = lights[hit.lightIndex].color;
            }
            break;
        }

        Surface surface = hit.surface;
        if (bounce == 0)
        {
            result.primaryDepth = linearViewDepth(surface.position, sceneUniforms.view);
            result.primaryNormal = surface.geometricNormal;
            result.primaryAlbedo = surface.brdf.albedo();
            result.emission = surface.emissive;
            primarySurface = restirPackSurface(surface, result.primaryDepth);
            // Use the stored material and directions consistently in initial and reuse target functions.
            primaryReservoir = restirGenerateInitial(restirUnpackSurface(primarySurface), state);
        }
        else
        {
            RestirReservoir reservoir = restirGenerateInitial(surface, state);
            result.indirectRadiance += throughput * restirShade(surface, reservoir);
        }
        if (bounce + 1 == parameters.maxBounces)
        {
            break;
        }

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
            if (survivalProbability == 0.0 || randomFloat(state) >= survivalProbability)
            {
                break;
            }
            throughput /= survivalProbability;
        }
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
    payload.barycentrics = attributes.barycentrics;
}

[shader("closesthit")]
void lightClosestHitMain(inout PathHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    hit.event = PathEvent::Emitter;
    hit.lightIndex = InstanceID();
    hit.distance = RayTCurrent();
}

#endif
