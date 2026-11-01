#include "raytracing/offline/offline_path_tracing.slang"
#include "common/low_discrepancy.slang"
#include "common/random.slang"

struct OfflineVertex { float3 position; float3 normal; float2 uv; };

[[vk::binding(OfflineRayTracingBinding::kColor, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(OfflineRayTracingBinding::kAccumulation, 3)]] RWStructuredBuffer<float4> accumulation;

OfflineVertex loadVertex(OfflineHitData object, uint index)
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
    ray.TMin = kOfflineRayEpsilon;
    ray.TMax = kOfflineMaxRayDistance;

    uint sampleState = parameters.samples.x;
    uint pathState = scrambleSeeds.y ^ randomUint(sampleState);
    float3 color = traceOfflinePath(ray, pathState);

    float3 sum = color;

    if (parameters.samples.x != 0)
    {
        sum += accumulation[pixelIndex].rgb;
    }

    accumulation[pixelIndex] = float4(sum, 0.0);
    outputImage[pixel] = float4(sum / (float(parameters.samples.x) + 1.0), 1.0);
}

[shader("miss")]
void missMain(inout OfflineHit hit) { hit.hit = 0; }

[shader("miss")]
void visibilityMissMain(inout OfflineVisibility visibility) { visibility.visible = 1; }

[shader("closesthit")]
void closestHitMain(inout OfflineHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    OfflineHitData object = objects[InstanceID()];
    uint* indices = (uint*)object.indexAddress;
    uint first = object.material.x + PrimitiveIndex() * 3;
    OfflineVertex v0 = loadVertex(object, indices[first]);
    OfflineVertex v1 = loadVertex(object, indices[first + 1]);
    OfflineVertex v2 = loadVertex(object, indices[first + 2]);
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
