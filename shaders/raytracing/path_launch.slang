#ifndef RENDERLAB_PATH_LAUNCH_SLANG
#define RENDERLAB_PATH_LAUNCH_SLANG

#include "path_common.slang"
#include "material/normal_mapping.slang"
#include "raytracing/camera_ray.slang"
#include "render/raytracing/PathTracingParameters.h"
#include "raytracing/geometry.slang"

struct CameraPathSample
{
    RayDesc ray;
    uint randomState;
};

CameraPathSample sampleCameraPath(uint2 pixel, uint pixelIndex, PathTracingParameters path)
{
    uint scrambleState = pixelIndex;
    uint2 scrambleSeeds;
    scrambleSeeds.x = randomUint(scrambleState);
    scrambleSeeds.y = randomUint(scrambleState);
    float2 pixelSample = sampleSobol2D(path.sampleIndex, scrambleSeeds);
    RayDesc ray = createCameraRay(pixel, pixelSample);

    uint sampleState = path.sampleIndex;
    return {ray, scrambleSeeds.y ^ randomUint(sampleState)};
}

[shader("miss")]
void missMain(inout Hit hit) { hit.hit = 0; }

[shader("miss")]
void visibilityMissMain(inout Visibility visibility) { visibility.visible = 1; }

[shader("closesthit")]
void closestHitMain(inout Hit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    HitData object = scene.objects[InstanceID()];

    // 取三角形顶点数据并插值
    uint* indices = (uint*)object.indexAddress;
    uint first = object.firstIndex + PrimitiveIndex() * 3;
    VertexAttributes v0 = loadVertexAttributes(object.vertexAddress, indices[first]);
    VertexAttributes v1 = loadVertexAttributes(object.vertexAddress, indices[first + 1]);
    VertexAttributes v2 = loadVertexAttributes(object.vertexAddress, indices[first + 2]);

    float3 weights = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
    float3 objectNormal = v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z;
    float4 objectTangent = v0.tangent * weights.x + v1.tangent * weights.y + v2.tangent * weights.z;

    // 记录数据
    hit.hit = 1;
    hit.materialIndex = object.materialIndex;
    hit.primitiveIndex = PrimitiveIndex();
    hit.emissiveTriangleIndex = emissiveLights.triangleIndex(InstanceID(), hit.primitiveIndex);
    hit.position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    hit.uv = v0.uv * weights.x + v1.uv * weights.y + v2.uv * weights.z;
    hit.normal = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));
    float3x3 model = (float3x3)ObjectToWorld3x4();
    hit.tangent = mul(model, objectTangent.xyz);
    float3 objectGeometricNormal = cross(v1.position - v0.position, v2.position - v0.position);
    hit.geometricNormal = normalize(mul(objectGeometricNormal, (float3x3)WorldToObject3x4()));
    if (object.reverseOrientation != 0)
    {
        hit.normal = -hit.normal;
        hit.geometricNormal = -hit.geometricNormal;
    }
    MaterialData material = materials[hit.materialIndex];
    if (material.kind == MaterialKind::Gltf)
    {
        float handedness = determinant(model) < 0.0 ? -1.0 : 1.0;
        float4 worldTangent = float4(hit.tangent, sign(objectTangent.w) * handedness);
        hit.normal = applyNormalMap(hit.normal, worldTangent,
            sampleTextureLevel(material.normalTexture, hit.uv).xyz, material.normalScale, material.normalEncoding);
    }
}

#endif
