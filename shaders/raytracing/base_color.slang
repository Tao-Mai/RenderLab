struct Payload { float3 color; };

struct HitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    float4 baseColorFactor;
    uint firstIndex;
    uint textureIndex;
    uint2 padding;
};

struct CameraData
{
    float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 viewport;
};

[[vk::push_constant]] ConstantBuffer<CameraData> camera;
[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene;
[[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> outputImage;
[[vk::binding(2, 0)]] StructuredBuffer<HitData> objects;
[[vk::binding(3, 0)]] Texture2D<float4> textures[128];
[[vk::binding(4, 0)]] SamplerState samplers[128];

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    float2 local = float2(pixel) + 0.5 - camera.viewport.xy;
    if (any(local < 0.0) || any(local >= camera.viewport.zw))
    {
        outputImage[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float2 ndc = local / camera.viewport.zw * 2.0 - 1.0;
    float4 farPoint = mul(camera.inverseViewProjection, float4(ndc, 1.0, 1.0));
    float3 toFar = farPoint.xyz / farPoint.w - camera.cameraPosition.xyz;
    RayDesc ray;
    ray.Origin = camera.cameraPosition.xyz;
    ray.Direction = normalize(toFar);
    ray.TMin = 0.001;
    ray.TMax = length(toFar);
    Payload payload = {float3(0.0)};
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);
    outputImage[pixel] = float4(payload.color, 1.0);
}

[shader("miss")]
void missMain(inout Payload payload)
{
    payload.color = float3(0.0);
}

[shader("closesthit")]
void closestHitMain(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    HitData object = objects[InstanceID()];
    if (object.textureIndex == ~0u)
    {
        payload.color = object.baseColorFactor.rgb;
        return;
    }
    uint* indices = (uint*)object.indexAddress;
    // Reuse the existing 32-byte Vertex layout (position, normal, texcoord).
    float* vertices = (float*)object.vertexAddress;
    uint first = object.firstIndex + PrimitiveIndex() * 3;
    uint i0 = indices[first] * 8 + 6;
    uint i1 = indices[first + 1] * 8 + 6;
    uint i2 = indices[first + 2] * 8 + 6;
    float3 barycentric = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y,
        attributes.barycentrics);
    float2 uv = float2(vertices[i0], vertices[i0 + 1]) * barycentric.x
        + float2(vertices[i1], vertices[i1 + 1]) * barycentric.y
        + float2(vertices[i2], vertices[i2 + 1]) * barycentric.z;
    uint textureIndex = NonUniformResourceIndex(object.textureIndex);
    payload.color = textures[textureIndex].SampleLevel(samplers[textureIndex], uv, 0).rgb
        * object.baseColorFactor.rgb;
}
