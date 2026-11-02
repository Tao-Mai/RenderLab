#if BDPT
#include "raytracing/offline/bdpt.slang"
#else
#include "raytracing/offline/pt.slang"
#endif

Vertex loadVertex(HitData object, uint index)
{
    float* vertex = (float*)object.vertexAddress + index * 12;

    return {float3(vertex[0], vertex[1], vertex[2]), float3(vertex[3], vertex[4], vertex[5]),
        float2(vertex[6], vertex[7]), float3(vertex[8], vertex[9], vertex[10])};
}

[shader("raygeneration")]
void raygenMain()
{
    // 获取像素索引
    uint2 pixel = DispatchRaysIndex().xy + parameters.tileOffset;
    uint pixelIndex = pixel.x + pixel.y * parameters.imageSize.x;
    float2 localPixel = float2(pixel) + 0.5 - parameters.viewportOffset;
    if (any(localPixel < 0.0) || any(localPixel >= parameters.viewportSize))
    {
        accumulation[pixelIndex] = (AccumulatedPixel)0;

        return;
    }

    // 低差异化序列+随机数 作为像素内抖动采样点
    uint scrambleState = pixelIndex;
    uint2 scrambleSeeds;
    scrambleSeeds.x = randomUint(scrambleState);
    scrambleSeeds.y = randomUint(scrambleState);
    float2 pixelSample = sampleSobol2D(parameters.accumulatedSamples, scrambleSeeds);
    float2 ndc = (float2(pixel) + pixelSample - parameters.viewportOffset) / parameters.viewportSize * 2.0 - 1.0;
    float4 farPoint = mul(sceneUniforms.inverseViewProjection, float4(ndc, 1.0, 1.0));

    // 构建光线
    RayDesc ray;
    ray.Origin = sceneUniforms.cameraPosition.xyz;
    ray.Direction = normalize(farPoint.xyz / farPoint.w - ray.Origin);
    ray.TMin = kRayEpsilon;
    ray.TMax = kMaxRayDistance;

    // 路径追踪
    uint sampleState = parameters.accumulatedSamples;
    uint pathState = scrambleSeeds.y ^ randomUint(sampleState);
    float3 color = tracePath(ray, pathState);

    // 累加颜色，并计算平均值
    float3 sum = color;

    if (parameters.accumulatedSamples != 0)
    {
        sum += accumulation[pixelIndex].color;
    }

    accumulation[pixelIndex] = {sum, parameters.accumulatedSamples + 1};
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
    Vertex v0 = loadVertex(object, indices[first]);
    Vertex v1 = loadVertex(object, indices[first + 1]);
    Vertex v2 = loadVertex(object, indices[first + 2]);

    float3 weights = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
    float3 objectNormal = v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z;
    float3 objectTangent = v0.tangent * weights.x + v1.tangent * weights.y + v2.tangent * weights.z;

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
