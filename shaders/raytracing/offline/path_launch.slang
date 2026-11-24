#ifndef RENDERLAB_OFFLINE_PATH_LAUNCH_SLANG
#define RENDERLAB_OFFLINE_PATH_LAUNCH_SLANG

#include "path_common.slang"
#include "raytracing/geometry.slang"

struct CameraPathSample
{
    RayDesc ray;
    uint randomState;
};

CameraPathSample sampleCameraPath(uint2 pixel, uint pixelIndex, PathParameters path)
{
    uint scrambleState = pixelIndex;
    uint2 scrambleSeeds;
    scrambleSeeds.x = randomUint(scrambleState);
    scrambleSeeds.y = randomUint(scrambleState);
    float2 pixelSample = sampleSobol2D(path.accumulatedSamples, scrambleSeeds);
    float2 ndc = uvToClip((float2(pixel) + pixelSample - path.viewportOffset) / path.viewportSize);
    float4 farPoint = mul(sceneUniforms.inverseViewProjection, float4(ndc, 1.0, 1.0));

    RayDesc ray;
    ray.Origin = sceneUniforms.cameraPosition.xyz;
    ray.Direction = normalize(farPoint.xyz / farPoint.w - ray.Origin);
    ray.TMin = kRayEpsilon;
    ray.TMax = kMaxRayDistance;

    uint sampleState = path.accumulatedSamples;
    return {ray, scrambleSeeds.y ^ randomUint(sampleState)};
}

void accumulateCameraSample(uint pixelIndex, uint completedSamples, float3 color)
{
    if (completedSamples != 0)
        color += accumulation[pixelIndex].color;
    accumulation[pixelIndex] = {color, completedSamples + 1};
}

[shader("miss")]
void missMain(inout Hit hit) { hit.hit = 0; }

[shader("miss")]
void visibilityMissMain(inout Visibility visibility) { visibility.visible = 1; }

[shader("closesthit")]
void closestHitMain(inout Hit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    HitData object = objects[InstanceID()];

    // 取三角形顶点数据并插值
    uint* indices = (uint*)object.indexAddress;
    uint first = object.firstIndex + PrimitiveIndex() * 3;
    VertexAttributes v0 = loadVertexAttributes(object.vertexAddress, indices[first]);
    VertexAttributes v1 = loadVertexAttributes(object.vertexAddress, indices[first + 1]);
    VertexAttributes v2 = loadVertexAttributes(object.vertexAddress, indices[first + 2]);

    float3 weights = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
    float3 objectNormal = v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z;
    float3 objectTangent = v0.tangent.xyz * weights.x + v1.tangent.xyz * weights.y + v2.tangent.xyz * weights.z;

    // 记录数据
    hit.hit = 1;
    hit.objectIndex = InstanceID();
    hit.primitiveIndex = PrimitiveIndex();
    hit.position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    hit.uv = v0.uv * weights.x + v1.uv * weights.y + v2.uv * weights.z;
    hit.normal = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));
    hit.tangent = mul((float3x3)ObjectToWorld3x4(), objectTangent);
    float3 objectGeometricNormal = cross(v1.position - v0.position, v2.position - v0.position);
    hit.geometricNormal = normalize(mul(objectGeometricNormal, (float3x3)WorldToObject3x4()));
    if (object.reverseOrientation)
    {
        hit.normal = -hit.normal;
        hit.geometricNormal = -hit.geometricNormal;
    }
}

#endif
