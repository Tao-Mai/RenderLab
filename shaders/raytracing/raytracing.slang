#include "../light_data.slang"
#include "../constant.slang"
#include "../random.slang"


static const uint kVertexStrideFloats = 12;
static const float kRayEpsilon = 0.001;
static const uint kVisibilityMissIndex = 1;
static const float kMaxRayDistance = 3.402823466e+38;
// Match the instance masks in RayTracingPass.cpp. Primary rays exclude light geometry.
static const uint kSceneMask = 0x01;
static const uint kLightMask = 0x02;

[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene;
[[vk::binding(3, 0)]] Texture2D<float4> textures[128];
[[vk::binding(4, 0)]] SamplerState samplers[128];
[[vk::binding(5, 0)]] StructuredBuffer<LightData> lights;

struct Payload
{
    float3 color;
    uint depth;
    uint randomState;
    float brdfPdf;
};

struct VisibilityPayload
{
    uint visible;
};

// Sampling remains analytic; intersection is handled by the light's two triangles in the TLAS.
struct RectAreaLight
{
    LightData data;

    float3 up()
    {
        return normalize(cross(data.direction.xyz, data.areaRight));
    }

    float3 samplePosition(inout uint state)
    {
        float u = (randomFloat(state) - 0.5) * data.areaSize.x;
        float v = (randomFloat(state) - 0.5) * data.areaSize.y;
        return data.position + data.areaRight * u + up() * v;
    }

    float pdf(float3 origin, float3 target)
    {
        float3 offset = target - origin;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0 || any(data.areaSize <= 0.0))
            return 0.0;

        float lightCosine = -dot(offset * rsqrt(distanceSquared), data.direction.xyz);
        if (lightCosine <= 0.0)
            return 0.0;

        return distanceSquared / (data.areaSize.x * data.areaSize.y * lightCosine);
    }
};

float balanceHeuristic(float samplePdf, float otherPdf)
{
    return samplePdf / (samplePdf + otherPdf);
}

bool isUnoccluded(RayDesc ray)
{
    VisibilityPayload payload = {0};
    TraceRay(
        scene,
        RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        kSceneMask | kLightMask, 0, 0, kVisibilityMissIndex, ray, payload
    );
    return payload.visible != 0;
}

struct Surface
{
    float3 position;
    float3 normal;
    float3 viewDirection; // World-space direction toward the previous ray vertex (wo).
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

        return isUnoccluded(ray);
    }

    float3 sampleDiffuseDirection(inout uint state)
    {
        float radius = sqrt(randomFloat(state));
        float phi = randomFloat(state) * 2.0 * PI;
        float3 tmp = abs(normal.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        float3 tangent = normalize(cross(normal, tmp));
        float3 bitangent = cross(normal, tangent);
        return normalize(
            radius * cos(phi) * tangent + radius * sin(phi) * bitangent +
            sqrt(max(0.0, 1.0 - radius * radius)) * normal
        );
    }

    float3 sampleDiffuseRadiance(uint depth, inout uint state)
    {
        RayDesc ray;
        ray.Origin = position + normal * kRayEpsilon;
        ray.Direction = sampleDiffuseDirection(state);
        ray.TMin = kRayEpsilon;
        ray.TMax = kMaxRayDistance;

        float brdfPdf = saturate(dot(normal, ray.Direction)) * INV_PI;
        Payload bounce = {float3(0.0), depth + 1, state, brdfPdf};
        TraceRay(
            scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
            kSceneMask | kLightMask, 0, 0, 0, ray, bounce
        );
        state = bounce.randomState;

        // The light hit shader applies MIS to emission; the surface hit shader returns unweighted indirect light.
        // Cosine-weighted Lambert sampling cancels BRDF * cos(theta) / PDF; shade applies baseColor once.
        return bounce.color;
    }

    float3 shade(uint lightCount, uint depth, inout uint state)
    {
        float3 radiance = float3(0.0);

        for (uint index = 0; index < lightCount; ++index)
        {
            LightData light = lights[index];
            if (!light.enabled)
                continue;

            if (light.type == LightType::Point) { 
                float3 toLight = light.position - position;
                float distanceSquared = dot(toLight, toLight);
                if (distanceSquared == 0.0)
                    continue;

                float3 lightDirection = toLight * rsqrt(distanceSquared);
                float nDotL = saturate(dot(normal, lightDirection));
                if (nDotL == 0.0 || !isVisible(light.position))
                    continue;

                float3 in_radiance = light.color * light.intensity / distanceSquared;
                float3 irradiance = in_radiance * nDotL;
                float brdf = INV_PI;
                radiance += irradiance * brdf;
            } else if (light.type == LightType::RectArea) {
                RectAreaLight emitter = {light};
                float3 origin = position + normal * kRayEpsilon;
                float3 sampleLightPosition = emitter.samplePosition(state);
                float3 toLight = sampleLightPosition - origin;
                float distanceSquared = dot(toLight, toLight);
                if (distanceSquared == 0.0)
                    continue;

                float3 lightDirection = toLight * rsqrt(distanceSquared);
                float nDotL = saturate(dot(normal, lightDirection));
                float lightPdf = emitter.pdf(origin, sampleLightPosition);
                if (nDotL == 0.0 || lightPdf == 0.0 || !isVisible(sampleLightPosition))
                    continue;

                // Every enabled light receives one NEE sample, so no light-selection probability is needed.
                float brdfPdf = nDotL * INV_PI;
                float weight = balanceHeuristic(lightPdf, brdfPdf);
                radiance += light.color * light.radiance * nDotL * INV_PI * weight / lightPdf;
            }
            
        }

        radiance += sampleDiffuseRadiance(depth, state);

        return baseColor * radiance;
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
            -normalize(WorldRayDirection()),
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
    uint maxRecursionDepth;
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
    Payload payload = {float3(0.0), 1, pixel.x + pixel.y * DispatchRaysDimensions().x, 0};
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = parameters.createRay(pixel);
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, kSceneMask, 0, 0, 0, ray, payload);
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
    // At the pipeline limit a ray may hit an emitter, but another surface cannot issue shadow rays.
    if (payload.depth >= parameters.maxRecursionDepth)
    {
        payload.color = float3(0.0);
        return;
    }

    HitData hit = objects[InstanceID()];
    Surface surface = hit.surfaceAt(PrimitiveIndex(), attributes.barycentrics);
    payload.color = surface.shade(parameters.lightCount, payload.depth, payload.randomState);
}

[shader("closesthit")]
void lightClosestHitMain(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    // Light instances use the SSBO light index as InstanceID(), not a mesh HitData index.
    RectAreaLight emitter = {lights[InstanceID()]};
    float3 origin = WorldRayOrigin();
    float3 target = origin + WorldRayDirection() * RayTCurrent();
    float lightPdf = emitter.pdf(origin, target);
    float weight = balanceHeuristic(payload.brdfPdf, lightPdf);
    payload.color = emitter.data.color * emitter.data.radiance * weight;
}
