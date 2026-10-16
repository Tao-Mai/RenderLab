#include "../light_data.slang"
#include "../constant.slang"
#include "../random.slang"
#include "../point_light_sampling.slang"


static const uint kVertexStrideFloats = 12;
static const uint kRussianRouletteStartDepth = 3;
static const float kRayEpsilon = 0.001;
static const uint kVisibilityMissIndex = 1;
static const float kMaxRayDistance = 3.402823466e+38;
// Match OfflinePathTracingPass.cpp. Physical emitters are visible to camera and bounce rays.
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
    float3 throughput;
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

struct Surface
{
    float3 position;
    float3 normal;
    float3 viewDirection; // World-space direction toward the previous ray vertex (wo).
    float3 baseColor;

    bool isVisible(float3 direction, float distance)
    {
        if (distance <= 2.0 * kRayEpsilon)
            return true;

        RayDesc ray;
        ray.Origin = position + normal * kRayEpsilon;
        ray.Direction = direction;
        ray.TMin = kRayEpsilon;
        ray.TMax = distance - kRayEpsilon;

        VisibilityPayload payload = {0};
        TraceRay(
            scene,
            RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
                RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            kSceneMask | kLightMask, 0, 0, kVisibilityMissIndex, ray, payload
        );
        return payload.visible != 0;
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

    float3 sampleDiffuseRadiance(uint depth, float3 throughput, inout uint state)
    {
        // Cosine-weighted Lambert sampling has BRDF * cosine / PDF = baseColor.
        float3 nextThroughput = throughput * baseColor;
        float survivalProbability = 1.0;
        if (depth >= kRussianRouletteStartDepth)
        {
            survivalProbability = saturate(max(nextThroughput.x, max(nextThroughput.y, nextThroughput.z)));
            if (survivalProbability == 0.0)
                return float3(0.0);

            if (survivalProbability < 1.0 && randomFloat(state) >= survivalProbability)
                return float3(0.0);

            nextThroughput /= survivalProbability;
        }

        RayDesc ray;
        ray.Origin = position + normal * kRayEpsilon;
        ray.Direction = sampleDiffuseDirection(state);
        ray.TMin = kRayEpsilon;
        ray.TMax = kMaxRayDistance;

        float brdfPdf = saturate(dot(normal, ray.Direction)) * INV_PI;
        Payload bounce = {float3(0.0), depth + 1, state, brdfPdf, nextThroughput};
        TraceRay(
            scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
            kSceneMask | kLightMask, 0, 0, 0, ray, bounce
        );
        state = bounce.randomState;

        // The light hit shader applies MIS to emission; the surface hit shader returns unweighted indirect light.
        // Cosine-weighted Lambert sampling cancels BRDF * cos(theta) / PDF; shade applies baseColor once.
        // Throughput guides later roulette decisions; recursive returns apply each factor exactly once.
        return bounce.color / survivalProbability;
    }

    float3 samplePointLightRadiance(LightRanges ranges, inout uint state)
    {
        PointLightSample selected = samplePointLight(lights, ranges.directionalEnd, ranges.pointEnd, position, state);
        if (selected.probability == 0.0)
            return float3(0.0);

        LightData light = lights[selected.index];
        float3 toLight = light.position - position;
        float distanceSquared = dot(toLight, toLight);
        float3 lightDirection = toLight * rsqrt(distanceSquared);
        float nDotL = saturate(dot(normal, lightDirection));
        if (nDotL == 0.0)
            return float3(0.0);

        float3 shadowOffset = light.position - (position + normal * kRayEpsilon);
        float shadowDistance = length(shadowOffset);
        if (!isVisible(normalize(shadowOffset), shadowDistance))
            return float3(0.0);

        // Delta point lights do not compete with the BRDF's continuous direction sampling.
        float3 incidentIrradiance = light.color / distanceSquared;
        return incidentIrradiance * nDotL * INV_PI / selected.probability;
    }

    float3 shade(LightRanges ranges, uint depth, float3 throughput, inout uint state)
    {
        float3 radiance = samplePointLightRadiance(ranges, state);
        for (uint index = 0; index < ranges.directionalEnd; ++index)
        {
            LightData light = lights[index];
            if (!light.enabled)
                continue;

            // Stored direction is light propagation; visibility points toward the source at infinity.
            float3 lightDirection = -normalize(light.direction.xyz);
            float nDotL = saturate(dot(normal, lightDirection));
            if (nDotL == 0.0 || !isVisible(lightDirection, kMaxRayDistance))
                continue;

            // A delta light has no competing diffuse-sampling path or distance attenuation.
            radiance += light.color * nDotL * INV_PI;
        }

        float3 origin = position + normal * kRayEpsilon;
        for (uint index = ranges.pointEnd; index < ranges.rectAreaEnd; ++index)
        {
            LightData light = lights[index];
            if (!light.enabled)
                continue;

            RectAreaLight emitter = {light};
            float3 sampleLightPosition = emitter.samplePosition(state);
            float3 toLight = sampleLightPosition - origin;
            float distanceSquared = dot(toLight, toLight);
            if (distanceSquared == 0.0)
                continue;

            float3 lightDirection = toLight * rsqrt(distanceSquared);
            float nDotL = saturate(dot(normal, lightDirection));
            float lightPdf = emitter.pdf(origin, sampleLightPosition);
            if (nDotL == 0.0 || lightPdf == 0.0 || !isVisible(lightDirection, sqrt(distanceSquared)))
                continue;

            // Every enabled area light receives one NEE sample; point-light selection does not enter this MIS.
            float brdfPdf = nDotL * INV_PI;
            float weight = balanceHeuristic(lightPdf, brdfPdf);
            radiance += light.color * nDotL * INV_PI * weight / lightPdf;
        }

        radiance += sampleDiffuseRadiance(depth, throughput, state);
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

struct OfflineParameters
{
    float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 viewport;
    LightRanges lightRanges;
    uint maxRecursionDepth;
    uint sampleIndex;
    uint padding0;
    uint padding1;

    bool containsPixel(uint2 pixel)
    {
        float2 local = float2(pixel) + 0.5 - viewport.xy;
        return all(local >= 0.0) && all(local < viewport.zw);
    }

    RayDesc createRay(uint2 pixel, float2 pixelSample)
    {
        float2 local = float2(pixel) + pixelSample - viewport.xy;
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

[[vk::push_constant]] ConstantBuffer<OfflineParameters> parameters;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;
[[vk::binding(2, 0)]] StructuredBuffer<HitData> objects;
[[vk::binding(6, 0)]] RWStructuredBuffer<float4> accumulation;

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint pixelIndex = pixel.x + pixel.y * DispatchRaysDimensions().x;
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;
    uint state = pixelIndex ^ (parameters.sampleIndex * kSampleSeedMultiplier);
    randomUint(state);
    float2 pixelSample = float2(randomFloat(state), randomFloat(state));
    Payload payload = {float3(0.0), 1, state, 0, float3(1.0)};
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = parameters.createRay(pixel, pixelSample);
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
                 kSceneMask | kLightMask, 0, 0, 0, ray, payload);
        float3 sum = payload.color;
        // First sample overwrites old contributions without reading uninitialized memory.
        if (parameters.sampleIndex != 0)
            sum += accumulation[pixelIndex].rgb;
        accumulation[pixelIndex] = float4(sum, 0.0);
        outputImage[pixel] = float4(sum / (float(parameters.sampleIndex) + 1.0), 1.0);
    }
    else
    {
        accumulation[pixelIndex] = float4(0.0);
        outputImage[pixel] = float4(0.0, 0.0, 0.0, 1.0);
    }
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
    payload.color = surface.shade(parameters.lightRanges, payload.depth, payload.throughput, payload.randomState);
}

[shader("closesthit")]
void lightClosestHitMain(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    // Light instances use the SSBO light index as InstanceID(), not a mesh HitData index.
    RectAreaLight emitter = {lights[InstanceID()]};
    float3 origin = WorldRayOrigin();
    float3 target = origin + WorldRayDirection() * RayTCurrent();
    float lightPdf = emitter.pdf(origin, target);
    if (lightPdf <= 0.0)
    {
        payload.color = float3(0.0);
        return;
    }
    float weight = payload.depth == 1 ? 1.0 : balanceHeuristic(payload.brdfPdf, lightPdf);
    payload.color = emitter.data.color * weight;
}
