#ifndef RENDERLAB_PATH_TRACING_SLANG
#define RENDERLAB_PATH_TRACING_SLANG

#include "math/constant.slang"
#include "material/material_evaluation.slang"
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
    PbrBrdf brdf;
    float3  emissive;
    uint    emissiveTriangleIndex;
    uint    materialIndex;

    float3 rayOrigin() { return position + geometricNormal * kRayEpsilon; }
    float pdf(float3 direction) { return brdf.pdf(normal, viewDirection, direction); }

    float3 evaluateLight(float3 direction, float3 incidentLight)
    {
        if (dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        return brdf.evaluate(normal, viewDirection, direction) * saturate(dot(normal, direction)) * incidentLight;
    }

    BrdfSample sample(inout SampleGenerator state)
    {
        float3 random;
        random.x = randomFloat(state);
        random.y = randomFloat(state);
        random.z = randomFloat(state);
        return brdf.sample(normal, viewDirection, random);
    }

    bool isVisible(float3 direction, float distance)
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

        static const uint kRayFlags =
            RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
        VisibilityPayload payload = {0};
        traceSceneRay(ray, RayType::Visibility, kRayFlags, payload);

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

    Surface surface;
    surface.position      = mul(hit.objectToWorld, float4(vertex.position, 1.0)).xyz;
    surface.viewDirection = -normalize(rayDirection);
    surface.normal        = applyNormalMap(worldNormal, worldTangent,
        sampleTextureLevel(material.normalTexture, vertex.uv).xyz, material.normalScale, material.normalEncoding);
    float facing = dot(geometricNormal, surface.viewDirection) >= 0.0 ? 1.0 : -1.0;
    surface.geometricNormal = geometricNormal * facing;
    surface.normal *= facing;
    if (dot(surface.normal, surface.viewDirection) <= 0.0)
    {
        surface.normal = surface.geometricNormal;
    }

    surface.brdf = evaluatePbrMaterial(material, vertex.uv).brdf;
    surface.materialIndex         = hit.materialIndex;
    surface.emissiveTriangleIndex = emissiveLights.triangleIndex(instanceIndex, primitiveIndex);
    surface.emissive = surface.emissiveTriangleIndex == kInvalidLightIndex
        ? float3(0.0)
        : emissiveRadiance(surface.emissiveTriangleIndex, vertex.uv, surface.viewDirection);
    return surface;
}

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


SampleGenerator createPathSampleGenerator(uint2 pixel, uint width, uint sampleIndex)
{
    uint pixelIndex = pixel.x + pixel.y * width;

#if defined(SAMPLE_GENERATOR_TYPE)
    return createUniformSampleGenerator(pixelIndex, sampleIndex);
#else
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;

    return pixelIndex ^ (sampleIndex * kSampleSeedMultiplier);
#endif
}

[shader("miss")] 
void missMain(inout PathHit hit) { hit.event = PathEvent::Miss; }

[shader("miss")] 
void visibilityMissMain(inout VisibilityPayload payload)
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
