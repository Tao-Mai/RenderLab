#include "../light_data.slang"
#include "../constant.slang"


static const uint kVertexStrideFloats = 12;
static const float kRayEpsilon = 0.001;
static const uint kVisibilityMissIndex = 1;

[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene;
[[vk::binding(3, 0)]] Texture2D<float4> textures[128];
[[vk::binding(4, 0)]] SamplerState samplers[128];
[[vk::binding(5, 0)]] StructuredBuffer<LightData> lights;

struct Payload
{
    float3 color;
};

struct VisibilityPayload
{
    uint visible;
};

struct Surface
{
    float3 position;
    float3 normal;
    float3 baseColor;

    bool isVisible(float3 target)
    {
        float3 origin = position + normal * kRayEpsilon;
        float3 toTarget = target - origin;
        float distance = length(toTarget);
        if (distance <= 2.0 * kRayEpsilon)
            return true;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = toTarget / distance;
        ray.TMin = kRayEpsilon;
        ray.TMax = distance - kRayEpsilon;

        VisibilityPayload payload = {0};
        TraceRay(
            scene,
            RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            0xff, 0, 0, kVisibilityMissIndex, ray, payload
        );
        return payload.visible != 0;
    }

    float3 shade(uint lightCount)
    {
        for (uint index = 0; index < lightCount; ++index)
        {
            LightData light = lights[index];
            if (!light.enabled)
                continue;

            if (light.type == LightType::Point) { 
                float3 toLight = light.position - position;
                float distanceSquared = dot(toLight, toLight);
                if (distanceSquared == 0.0)
                    return float3(0.0);

                float3 lightDirection = toLight * rsqrt(distanceSquared);
                float nDotL = saturate(dot(normal, lightDirection));
                if (nDotL == 0.0 || !isVisible(light.position))
                    return float3(0.0);

                float3 radiance = light.color * light.intensity / distanceSquared;
                float3 irradiance = radiance * nDotL;

                return baseColor * irradiance * INV_PI;
            }
            
        }
        return float3(0.0);
    }
};

struct VertexAttributes
{
    float3 normal;
    float2 uv;
};

struct HitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    float4 baseColorFactor;
    uint firstIndex;
    uint textureIndex;
    uint2 padding;

    VertexAttributes loadVertex(uint index)
    {
        // Matches the packed 48-byte C++ Vertex; do not apply std430 float3 alignment.
        float* vertex = (float*)vertexAddress + index * kVertexStrideFloats;
        return {
            float3(vertex[3], vertex[4], vertex[5]),
            float2(vertex[6], vertex[7]),
        };
    }

    Surface surfaceAt(uint primitiveIndex, float2 barycentrics)
    {
        uint* indices = (uint*)indexAddress;
        uint first = firstIndex + primitiveIndex * 3;
        VertexAttributes v0 = loadVertex(indices[first]);
        VertexAttributes v1 = loadVertex(indices[first + 1]);
        VertexAttributes v2 = loadVertex(indices[first + 2]);
        float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics);
        float2 uv = v0.uv * weights.x + v1.uv * weights.y + v2.uv * weights.z;
        float3 objectNormal = v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z;
        uint materialIndex = NonUniformResourceIndex(textureIndex);

        // Row-vector multiplication applies the inverse transpose, including nonuniform scale.
        return {
            WorldRayOrigin() + WorldRayDirection() * RayTCurrent(),
            normalize(mul(objectNormal, (float3x3)WorldToObject3x4())),
            textures[materialIndex].SampleLevel(samplers[materialIndex], uv, 0).rgb * baseColorFactor.rgb,
        };
    }
};

struct RayTracingParameters
{
    float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 viewport;
    uint lightCount;
    uint padding0;
    uint padding1;
    uint padding2;

    bool containsPixel(uint2 pixel)
    {
        float2 local = float2(pixel) + 0.5 - viewport.xy;
        return all(local >= 0.0) && all(local < viewport.zw);
    }

    RayDesc createRay(uint2 pixel)
    {
        float2 local = float2(pixel) + 0.5 - viewport.xy;
        float2 ndc = local / viewport.zw * 2.0 - 1.0;
        float4 farPoint = mul(inverseViewProjection, float4(ndc, 1.0, 1.0));
        float3 toFar = farPoint.xyz / farPoint.w - cameraPosition.xyz;

        RayDesc ray;
        ray.Origin = cameraPosition.xyz;
        ray.Direction = normalize(toFar);
        ray.TMin = kRayEpsilon;
        ray.TMax = length(toFar);
        return ray;
    }
};

[[vk::push_constant]] ConstantBuffer<RayTracingParameters> parameters;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(2, 0)]] StructuredBuffer<HitData> objects;

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    Payload payload = {float3(0.0)};
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = parameters.createRay(pixel);
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);
    }
    outputImage[pixel] = float4(payload.color, 1.0);
}

[shader("miss")]
void missMain(inout Payload payload)
{
    payload.color = float3(0.0);
}

[shader("miss")]
void visibilityMissMain(inout VisibilityPayload payload)
{
    payload.visible = 1;
}

[shader("closesthit")]
void closestHitMain(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    HitData hit = objects[InstanceID()];
    Surface surface = hit.surfaceAt(PrimitiveIndex(), attributes.barycentrics);
    payload.color = surface.shade(parameters.lightCount);
}
