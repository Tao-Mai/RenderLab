#ifndef RENDERLAB_PATH_TRACING_SLANG
#define RENDERLAB_PATH_TRACING_SLANG

#include "math/constant.slang"
#include "Rendering/Materials/StandardMaterial.slang"
#include "material/normal_mapping.slang"
#include "Utils/Sampling/SampleGenerator.slang"
#include "raytracing/camera_ray.slang"
#include "Rendering/Lights/EmissiveLightSamplerHelpers.slang"
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
    float3  geometricNormal; // 网格的normal
    float3  viewDirection;  // World-space direction toward the previous ray vertex (wo).
    StandardMaterialInstance material;
    float3  emissive;
    uint    emissiveTriangleIndex;
    uint    materialIndex;

    float3 rayOrigin() { return position + geometricNormal * kRayEpsilon; }
    float3 rayOrigin(float3 direction)
    {
        return position + geometricNormal * (dot(geometricNormal, direction) >= 0.0 ? kRayEpsilon : -kRayEpsilon);
    }

    bool validHemisphere(float3 direction)
    {
        return dot(material.frame.normal, viewDirection) >= kMinCosTheta &&
            ((dot(material.frame.normal, direction) >= 0.0) == (dot(geometricNormal, direction) >= 0.0));
    }

    float pdf(float3 direction)
    {
        return validHemisphere(direction) ? material.evalPdf(viewDirection, direction) : 0.0;
    }

    float3 evaluateLight(float3 direction, float3 incidentLight)
    {
        if (!validHemisphere(direction))
        {
            return float3(0.0);
        }
        return material.eval(viewDirection, direction) * incidentLight;
    }

    BSDFSample sample(inout SampleGenerator state)
    {
        BSDFSample result = material.sample(viewDirection, state);
        result.valid = result.valid && validHemisphere(result.direction);

        return result;
    }

    bool isVisible(float3 direction, float distance)
    {
        if (distance <= 2.0 * kRayEpsilon)
        {
            return true;
        }

        RayDesc ray;
        ray.Origin    = rayOrigin(direction);
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
    StandardMaterialData material = materials[hit.materialIndex];

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
    float3 shadingNormal = applyNormalMap(worldNormal, worldTangent,
        sampleMaterialTexture(material.normalTexture, vertex.uv, float4(0.5, 0.5, 1.0, 1.0)).xyz, material.normalScale, material.normalEncoding);
    bool frontFacing = dot(geometricNormal, surface.viewDirection) >= 0.0;
    surface.geometricNormal = frontFacing ? geometricNormal : -geometricNormal;
    if (!frontFacing && material.doubleSided != 0)
        shadingNormal = -shadingNormal;

    // Match Falcor's grazing-angle shading-normal adjustment without flipping single-sided backfaces.
    float signNormal = dot(shadingNormal, surface.geometricNormal) >= 0.0 ? 1.0 : -1.0;
    float3 orientedNormal = signNormal * shadingNormal;
    static const float kCosThetaThreshold = 0.1;
    float cosine = dot(surface.viewDirection, orientedNormal);
    if (cosine <= kCosThetaThreshold)
        shadingNormal = normalize(signNormal * lerp(surface.geometricNormal, orientedNormal, saturate(cosine / kCosThetaThreshold)));

    StandardMaterial standardMaterial = {material};
    surface.material = standardMaterial.setupMaterialInstance(vertex.uv, shadingNormal, frontFacing);
    surface.materialIndex         = hit.materialIndex;
    surface.emissiveTriangleIndex = emissiveLights.triangleIndex(instanceIndex, primitiveIndex);
    surface.emissive = surface.material.emission;

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
};


SampleGenerator createPathSampleGenerator(uint2 pixel, uint width, uint sampleSeed)
{
    uint pixelIndex = pixel.x + pixel.y * width;

    return SampleGenerator(pixelIndex, sampleSeed);
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
}

#endif
