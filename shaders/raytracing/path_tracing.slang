#ifndef RENDERLAB_PATH_TRACING_SLANG
#define RENDERLAB_PATH_TRACING_SLANG

#include "render/raytracing/PathTracingParameters.h"

#include "math/constant.slang"
#include "material/normal_mapping.slang"
#include "material/scattering.slang"
#include "math/random.slang"
#include "scene/scene_data.slang"
#include "scene/light_emitters.slang"
#include "scene_hits.slang"
#include "geometry.slang"

#ifndef HAS_ENV
#error HAS_ENV must be specified when compiling path tracing
#endif

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

    pbrt::BsdfSample sample(inout uint state)
    {
        return bsdf().sample(state);
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

        VisibilityPayload payload = {0};
        TraceRay(
            scene,
            RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            kSceneMask | kLightMask,
            0,
            0,
            kVisibilityMissIndex,
            ray,
            payload
        );
        return payload.visible != 0;
    }
};

Surface surfaceAt(HitData hit, uint primitiveIndex, float2 barycentrics)
{
    uint*            indices       = (uint*)hit.indexAddress;
    uint             first         = hit.firstIndex + primitiveIndex * 3;
    VertexAttributes v0            = loadVertexAttributes(hit.vertexAddress, indices[first]);
    VertexAttributes v1            = loadVertexAttributes(hit.vertexAddress, indices[first + 1]);
    VertexAttributes v2            = loadVertexAttributes(hit.vertexAddress, indices[first + 2]);
    float3           weights       = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics);
    float2           uv            = v0.uv * weights.x + v1.uv * weights.y + v2.uv * weights.z;
    float3           objectNormal  = v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z;
    float4           objectTangent = v0.tangent * weights.x + v1.tangent * weights.y + v2.tangent * weights.z;
    MaterialData     material      = materials[hit.materialIndex];

    // Row-vector multiplication applies the inverse transpose, including nonuniform scale.
    float3   worldNormal  = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));
    float3x3 model        = (float3x3)ObjectToWorld3x4();
    float    handedness   = determinant(model) < 0.0 ? -1.0 : 1.0;
    float4   worldTangent = float4(mul(model, objectTangent.xyz), sign(objectTangent.w) * handedness);
    float3   geometricNormal =
        normalize(cross(mul(model, v1.position - v0.position), mul(model, v2.position - v0.position)));
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
    surface.position      = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    surface.viewDirection = -normalize(WorldRayDirection());
    surface.normal        = worldNormal;
    if (material.kind == MaterialKind::Gltf)
        surface.normal = applyNormalMap(worldNormal, worldTangent,
            sampleTextureLevel(material.normalTexture, uv).xyz, material.normalScale, material.normalEncoding);
    surface.tangent = worldTangent.xyz;
    surface.frontFace = dot(geometricNormal, surface.viewDirection) >= 0.0;
    float facing = surface.frontFace ? 1.0 : -1.0;
    surface.geometricNormal = geometricNormal * facing;
    surface.normal *= facing;
    if (dot(surface.normal, surface.viewDirection) <= 0.0)
    {
        surface.normal = surface.geometricNormal;
    }

    surface.scattering = evaluateScattering(material, uv, surface.position, worldNormal, primitiveIndex);
    surface.materialIndex         = hit.materialIndex;
    surface.objectIndex           = InstanceID();
    surface.emissiveTriangleIndex = hit.firstAreaLight == kInvalidLightIndex
        ? kInvalidLightIndex
        : hit.firstAreaLight + primitiveIndex;
    surface.emissive = surface.emissiveTriangleIndex == kInvalidLightIndex
        ? float3(0.0)
        : areaEmission(surface.emissiveTriangleIndex, uv, surface.viewDirection);
    return surface;
}

ConstantBuffer<PathTracingParameters> parameters;

RayDesc createPathRay(uint2 pixel, float2 pixelSample)
{
    float2 local    = float2(pixel) + pixelSample - parameters.viewportOffset;
    float2 ndc      = uvToClip(local / parameters.viewportExtent);
    float4 farPoint = mul(sceneGlobals[0].camera.inverseViewProjection, float4(ndc, 1.0, 1.0));
    float3 toFar    = farPoint.xyz / farPoint.w - sceneGlobals[0].camera.cameraPosition.xyz;

    RayDesc ray;
    ray.Origin    = sceneGlobals[0].camera.cameraPosition.xyz;
    ray.Direction = normalize(toFar);
    ray.TMin      = kRayEpsilon;
    ray.TMax      = kMaxRayDistance;
    return ray;
}

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
    uint      lightIndex;
    Surface   surface;
    float2    barycentrics;
};

bool advancePath(
    Surface       surface,
    pbrt::BsdfSample sampled,
    uint          scatteringCount,
    inout float3  throughput,
    inout RayDesc ray,
    inout uint    state
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

uint pathRandomState(uint2 pixel, uint width)
{
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;
    return (pixel.x + pixel.y * width) ^ (parameters.sampleIndex * kSampleSeedMultiplier);
}

float3 hitEmission(RayDesc ray, PathHit hit)
{
    if (hit.event == PathEvent::Surface)
        return hit.surface.emissive;
    if (hit.event == PathEvent::Miss)
        return environmentRadiance(ray.Direction);
    LightTriangle triangle = loadTriangleLight(hit.lightIndex);
    float2 uv = triangleUv(triangle, float3(1.0 - hit.barycentrics.x - hit.barycentrics.y, hit.barycentrics));
    return areaEmission(hit.lightIndex, uv, -ray.Direction);
}

[shader("miss")] void missMain(inout PathHit hit) { hit.event = PathEvent::Miss; }

    [shader("miss")] void visibilityMissMain(inout VisibilityPayload payload)
{
    payload.visible = 1;
}

[shader("closesthit")] void closestHitMain(inout PathHit payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.event         = PathEvent::Surface;
    HitData hit = objects[InstanceID()];
    payload.surface       = surfaceAt(hit, PrimitiveIndex(), attributes.barycentrics);
    payload.barycentrics  = attributes.barycentrics;
}

    [shader("closesthit")] void lightClosestHitMain(inout PathHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    hit.event      = PathEvent::Emitter;
    hit.lightIndex = rectTriangleIndex(InstanceID(), PrimitiveIndex());
    hit.barycentrics = attributes.barycentrics;
}

#endif
