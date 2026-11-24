#ifndef RENDERLAB_PATH_TRACING_SLANG
#define RENDERLAB_PATH_TRACING_SLANG

#include "render/raytracing/PathTracingParameters.h"

#include "math/constant.slang"
#include "material/normal_mapping.slang"
#include "math/random.slang"
#include "raytracing/camera_ray.slang"
#include "scene/emissive_lights.slang"
#include "scene/environment_light.slang"
#include "scene_hits.slang"
#include "geometry.slang"

static const uint  kRussianRouletteStartBounce = 3;

struct VisibilityPayload
{
    uint visible;
};

struct Surface
{
    float3  position;
    float3  geometricNormal;
    float3  normal;
    float3  viewDirection;  // World-space direction toward the previous ray vertex (wo).
    float3 tangent;
    bool frontFace;
    ScatteringMaterial scattering;
    float3  emissive;
    uint    emissiveTriangleIndex;
    uint    materialIndex;
    uint    objectIndex;

    float3 rayOrigin() { return position + geometricNormal * kRayEpsilon; }
    SurfaceBsdf bsdf() { return {scattering, normal, tangent, viewDirection, frontFace}; }
    float3 albedo() { return scattering.rasterBrdf().albedo(); }
    float pdf(float3 direction) { return bsdf().pdf(direction); }

    float3 evaluateLight(float3 direction, float3 incidentLight)
    {
        if (dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        return bsdf().evaluate(direction) * saturate(dot(normal, direction)) * incidentLight;
    }

    pbrt::BsdfSample sample(inout SampleGenerator state)
    {
        return bsdf().sample(state);
    }

    bool isVisible(float3 direction, float distance, uint hitGroup = 0,
        uint rayFlags = RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES)
    {
        if (distance <= 2.0 * kRayEpsilon)
        {
            return true;
        }

        RayDesc ray;
        ray.Origin    = rayOrigin();
        ray.Direction = direction;
        ray.TMin      = kRayEpsilon;
        ray.TMax      = distance - kRayEpsilon;

        VisibilityPayload payload = {0};
        TraceRay(
            scene.rtAccel,
            rayFlags | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            kSceneMask,
            hitGroup,
            0,
            kVisibilityMissIndex,
            ray,
            payload
        );
        return payload.visible != 0;
    }
};

Surface surfaceAt(uint instanceIndex, uint primitiveIndex, float2 barycentrics, float3 rayDirection)
{
    HitData hit = scene.objects[instanceIndex];
    TriangleVertices triangle = loadTriangleVertices(hit.vertexAddress, hit.indexAddress, hit.firstIndex + primitiveIndex * 3);
    VertexAttributes vertex = triangle.interpolate(barycentrics);
    MaterialData material = materials[hit.materialIndex];

    // Row-vector multiplication applies the inverse transpose, including nonuniform scale.
    float3   worldNormal  = normalize(mul(vertex.normal, (float3x3)hit.worldToObject));
    float3x3 model        = (float3x3)hit.objectToWorld;
    float    handedness   = determinant(model) < 0.0 ? -1.0 : 1.0;
    float4   worldTangent = float4(mul(model, vertex.tangent.xyz), sign(vertex.tangent.w) * handedness);
    float3 edge1 = mul(model, triangle.v1.position - triangle.v0.position);
    float3 edge2 = mul(model, triangle.v2.position - triangle.v0.position);
    float3 geometricNormal = normalize(cross(edge1, edge2));
    if (dot(geometricNormal, worldNormal) < 0.0)
    {
        geometricNormal = -geometricNormal;
    }
    if (hit.reverseOrientation != 0)
    {
        worldNormal = -worldNormal;
        geometricNormal = -geometricNormal;
    }

    Surface surface;
    surface.position      = mul(hit.objectToWorld, float4(vertex.position, 1.0)).xyz;
    surface.viewDirection = -normalize(rayDirection);
    surface.normal        = worldNormal;
    if (material.kind == MaterialKind::Gltf)
        surface.normal = applyNormalMap(worldNormal, worldTangent,
            sampleTextureLevel(material.normalTexture, vertex.uv).xyz, material.normalScale, material.normalEncoding);
    surface.tangent = worldTangent.xyz;
    surface.frontFace = dot(geometricNormal, surface.viewDirection) >= 0.0;
    float facing = surface.frontFace ? 1.0 : -1.0;
    surface.geometricNormal = geometricNormal * facing;
    surface.normal *= facing;
    if (dot(surface.normal, surface.viewDirection) <= 0.0)
    {
        surface.normal = surface.geometricNormal;
    }

    surface.scattering = evaluateScattering(material, vertex.uv, surface.position, worldNormal, primitiveIndex);
    surface.materialIndex         = hit.materialIndex;
    surface.objectIndex           = instanceIndex;
    surface.emissiveTriangleIndex = emissiveLights.triangleIndex(surface.objectIndex, primitiveIndex);
    surface.emissive = surface.emissiveTriangleIndex == kInvalidLightIndex
        ? float3(0.0)
        : emissiveRadiance(surface.emissiveTriangleIndex, vertex.uv, surface.viewDirection);
    return surface;
}

ConstantBuffer<PathTracingParameters> parameters;

enum class PathEvent : uint
{
    Miss,
    Surface,
};

// Hit shaders only return a result. All path and visibility rays originate in RayGen.
struct PathHit
{
    PathEvent event;
    Surface   surface;
    float2    barycentrics;
};

bool advancePath(
    Surface       surface,
    pbrt::BsdfSample sampled,
    uint          scatteringCount,
    inout float3  throughput,
    inout RayDesc ray,
    inout SampleGenerator state
)
{
    const float side = dot(surface.geometricNormal, sampled.direction);
    if ((sampled.delta == 0 && sampled.pdf <= 0.0) ||
        (!surface.scattering.canTransmit() && side <= 0.0) || !any(sampled.weight > 0.0))
        return false;

    throughput *= sampled.weight;
    if (scatteringCount >= kRussianRouletteStartBounce)
    {
        float survivalProbability = saturate(max(throughput.x, max(throughput.y, throughput.z)));
        if (survivalProbability == 0.0 || randomFloat(state) >= survivalProbability)
            return false;
        throughput /= survivalProbability;
    }
    ray.Origin    = surface.position + surface.geometricNormal * (side < 0.0 ? -kRayEpsilon : kRayEpsilon);
    ray.Direction = sampled.direction;
    ray.TMin      = kRayEpsilon;
    ray.TMax      = kMaxRayDistance;
    return true;
}

SampleGenerator createPathSampleGenerator(uint2 pixel, uint width)
{
    uint pixelIndex = pixel.x + pixel.y * width;
#if defined(SAMPLE_GENERATOR_TYPE)
    return createUniformSampleGenerator(pixelIndex, parameters.sampleIndex);
#else
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;
    return pixelIndex ^ (parameters.sampleIndex * kSampleSeedMultiplier);
#endif
}

float3 hitEmission(RayDesc ray, PathHit hit)
{
    if (hit.event == PathEvent::Surface)
        return hit.surface.emissive;
    return environmentRadiance(ray.Direction);
}

[shader("miss")] void missMain(inout PathHit hit) { hit.event = PathEvent::Miss; }

    [shader("miss")] void visibilityMissMain(inout VisibilityPayload payload)
{
    payload.visible = 1;
}

[shader("closesthit")] void closestHitMain(inout PathHit payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.event         = PathEvent::Surface;
    payload.surface       = surfaceAt(InstanceID(), PrimitiveIndex(), attributes.barycentrics, WorldRayDirection());
    payload.barycentrics  = attributes.barycentrics;
}

#endif
