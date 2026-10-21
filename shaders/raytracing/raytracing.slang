#include "../light_data.slang"
#include "../constant.slang"
#include "../random.slang"
#include "../point_light_sampling.slang"
#include "../rect_area_light_sampling.slang"
#include "../material_data.slang"
#include "../pbr_brdf.slang"
#include "../normal_mapping.slang"
#include "../view_data.slang"


static const uint kVertexStrideFloats = 12;
static const uint kRussianRouletteStartDepth = 3;
static const float kRayEpsilon = 0.001;
static const uint kVisibilityMissIndex = 1;
static const float kMaxRayDistance = 3.402823466e+38;
// Match the instance masks in RayTracingPass.cpp. Primary rays exclude light geometry.
static const uint kSceneMask = 0x01;
static const uint kLightMask = 0x02;

[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene;
[[vk::binding(5, 0)]] StructuredBuffer<LightData> lights;

struct Payload
{
    float3 color;
    uint depth;
    uint randomState;
    float brdfPdf;
    float3 throughput;
    float rectAreaLightWeightSum; // Selection weights at this ray's origin, not the current hit.
    float primaryDepth; // Unnormalized positive view-space depth; -1 means no surface.
    float3 primaryNormal;
};

struct VisibilityPayload
{
    uint visible;
};

float balanceHeuristic(float samplePdf, float otherPdf)
{
    return samplePdf / (samplePdf + otherPdf);
}

struct Surface
{
    float3 position;
    float3 geometricNormal;
    float3 normal;
    float3 viewDirection; // World-space direction toward the previous ray vertex (wo).
    PbrBrdf brdf;
    float3 emissive;

    float3 rayOrigin()
    {
        return position + geometricNormal * kRayEpsilon;
    }

    float3 evaluateLight(float3 direction, float3 incidentLight)
    {
        if (dot(geometricNormal, direction) <= 0.0)
            return float3(0.0);
        return brdf.evaluate(normal, viewDirection, direction)
            * saturate(dot(normal, direction)) * incidentLight;
    }

    bool isVisible(float3 direction, float distance)
    {
        if (distance <= 2.0 * kRayEpsilon)
            return true;

        RayDesc ray;
        ray.Origin = rayOrigin();
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

    float3 sampleBrdfRadiance(uint depth, float3 throughput, float rectAreaLightWeightSum, inout uint state)
    {
        float3 randomSample;
        randomSample.x = randomFloat(state);
        randomSample.y = randomFloat(state);
        randomSample.z = randomFloat(state);
        BrdfSample sampled = brdf.sample(normal, viewDirection, randomSample);
        if (sampled.pdf <= 0.0 || dot(geometricNormal, sampled.direction) <= 0.0 || !any(sampled.weight > 0.0))
            return float3(0.0);

        float3 nextThroughput = throughput * sampled.weight;
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
        ray.Origin = rayOrigin();
        ray.Direction = sampled.direction;
        ray.TMin = kRayEpsilon;
        ray.TMax = kMaxRayDistance;

        Payload bounce = {float3(0.0), depth + 1, state, sampled.pdf, nextThroughput, rectAreaLightWeightSum,
            -1.0, float3(0.0)};
        TraceRay(
            scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
            kSceneMask | kLightMask, 0, 0, 0, ray, bounce
        );
        state = bounce.randomState;

        // Payload throughput guides roulette; recursive returns apply BRDF * cosine / PDF exactly once.
        return sampled.weight * bounce.color / survivalProbability;
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

        float3 shadowOffset = light.position - rayOrigin();
        float shadowDistance = length(shadowOffset);
        if (!isVisible(normalize(shadowOffset), shadowDistance))
            return float3(0.0);

        // Delta point lights do not compete with the BRDF's continuous direction sampling.
        float3 incidentIrradiance = light.color / distanceSquared;
        return evaluateLight(lightDirection, incidentIrradiance) / selected.probability;
    }

    float3 sampleRectAreaLightRadiance(RectAreaLightSample selected, inout uint state)
    {
        if (selected.probability == 0.0)
            return float3(0.0);

        float3 origin = rayOrigin();
        RectAreaLight emitter = {lights[selected.index]};
        float3 sampleLightPosition = emitter.samplePosition(state);
        float3 toLight = sampleLightPosition - origin;
        float distanceSquared = dot(toLight, toLight);
        if (distanceSquared == 0.0)
            return float3(0.0);

        float3 lightDirection = toLight * rsqrt(distanceSquared);
        float nDotL = saturate(dot(normal, lightDirection));
        float lightPdf = selected.probability * emitter.pdf(origin, sampleLightPosition);
        if (nDotL == 0.0 || lightPdf == 0.0 || !isVisible(lightDirection, sqrt(distanceSquared)))
            return float3(0.0);

        // The full NEE PDF includes discrete light selection and conditional surface sampling.
        float brdfPdf = dot(geometricNormal, lightDirection) > 0.0
            ? brdf.pdf(normal, viewDirection, lightDirection) : 0.0;
        float weight = balanceHeuristic(lightPdf, brdfPdf);
        return evaluateLight(lightDirection, emitter.data.color) * weight / lightPdf;
    }

    float3 shade(LightRanges ranges, uint depth, float3 throughput, inout uint state)
    {
        float3 radiance = samplePointLightRadiance(ranges, state);
        for (uint index = 0; index < ranges.directionalEnd; ++index)
        {
            LightData light = lights[index];

            // Stored direction is light propagation; visibility points toward the source at infinity.
            float3 lightDirection = -normalize(light.direction.xyz);
            float nDotL = saturate(dot(normal, lightDirection));
            if (nDotL == 0.0 || !isVisible(lightDirection, kMaxRayDistance))
                continue;

            // A delta light has no competing BRDF-sampling path or distance attenuation.
            radiance += evaluateLight(lightDirection, light.color);
        }

        // NEE and the BRDF bounce share the selection distribution at the same ray origin.
        float3 origin = rayOrigin();
        RectAreaLightSample selected = sampleRectAreaLight(lights, ranges.pointEnd, ranges.rectAreaEnd, origin, state);
        radiance += sampleRectAreaLightRadiance(selected, state);

        radiance += sampleBrdfRadiance(depth, throughput, selected.weightSum, state);
        return radiance;
    }
};

struct VertexAttributes
{
    float3 position;
    float3 normal;
    float2 uv;
    float4 tangent;
};

struct HitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint firstIndex;
    uint materialIndex;
    uint2 padding;

    VertexAttributes loadVertex(uint index)
    {
        // Matches the packed 48-byte C++ Vertex; do not apply std430 float3 alignment.
        float* vertex = (float*)vertexAddress + index * kVertexStrideFloats;
        return {
            float3(vertex[0], vertex[1], vertex[2]),
            float3(vertex[3], vertex[4], vertex[5]),
            float2(vertex[6], vertex[7]),
            float4(vertex[8], vertex[9], vertex[10], vertex[11]),
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
        float4 objectTangent = v0.tangent * weights.x + v1.tangent * weights.y + v2.tangent * weights.z;
        MaterialData material = materials[materialIndex];

        // Row-vector multiplication applies the inverse transpose, including nonuniform scale.
        float3 worldNormal = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));
        float3x3 model = (float3x3)ObjectToWorld3x4();
        float handedness = determinant(model) < 0.0 ? -1.0 : 1.0;
        float4 worldTangent = float4(mul(model, objectTangent.xyz), sign(objectTangent.w) * handedness);
        float3 geometricNormal = normalize(cross(mul(model, v1.position - v0.position),
            mul(model, v2.position - v0.position)));
        if (dot(geometricNormal, worldNormal) < 0.0)
            geometricNormal = -geometricNormal;

        Surface surface;
        surface.position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
        surface.viewDirection = -normalize(WorldRayDirection());
        surface.normal = applyNormalMap(worldNormal, worldTangent,
            sampleTextureLevel(material.normalTexture, uv).xyz, material.normalScale);
        float facing = dot(geometricNormal, surface.viewDirection) < 0.0 ? -1.0 : 1.0;
        surface.geometricNormal = geometricNormal * facing;
        surface.normal *= facing;
        if (dot(surface.normal, surface.viewDirection) <= 0.0)
            surface.normal = surface.geometricNormal;

        surface.brdf.baseColor = sampleTextureLevel(material.baseColorTexture, uv).rgb * material.baseColorFactor.rgb;
        surface.brdf.metallic = saturate(material.metallic * sampleTextureLevel(material.metallicTexture, uv).b);
        surface.brdf.roughness = saturate(material.roughness * sampleTextureLevel(material.roughnessTexture, uv).g);
        surface.emissive = sampleTextureLevel(material.emissiveTexture, uv).rgb * material.emissiveFactor.rgb;
        return surface;
    }
};

struct RayTracingParameters
{
    float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 viewport;
    LightRanges lightRanges;
    uint maxRecursionDepth;
    uint sampleIndex;
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
[[vk::binding(7, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> depthImage;
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> normalImage;
[[vk::binding(9, 0)]] ConstantBuffer<ViewUniforms> viewUniforms;

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;
    uint state = (pixel.x + pixel.y * DispatchRaysDimensions().x) ^ (parameters.sampleIndex * kSampleSeedMultiplier);
    Payload payload = {float3(0.0), 1, state, 0, float3(1.0), 0.0, -1.0, float3(0.0)};
    if (parameters.containsPixel(pixel))
    {
        RayDesc ray = parameters.createRay(pixel);
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, kSceneMask, 0, 0, 0, ray, payload);
    }
    outputImage[pixel] = float4(payload.color, 1.0);
    depthImage[pixel] = payload.primaryDepth;
    normalImage[pixel] = float4(payload.primaryNormal, 0.0);
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
    if (payload.depth == 1)
    {
        payload.primaryDepth = linearViewDepth(surface.position, viewUniforms.view);
        payload.primaryNormal = surface.geometricNormal;
    }
    payload.color = surface.emissive;
    // Terminal surface emission needs no extra ray; direct lighting would issue visibility rays.
    if (payload.depth >= parameters.maxRecursionDepth)
        return;

    payload.color += surface.shade(parameters.lightRanges, payload.depth, payload.throughput, payload.randomState);
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

    float selectionProbability = payload.rectAreaLightWeightSum > 0.0
        ? emitter.selectionWeight(origin) / payload.rectAreaLightWeightSum : 0.0;
    lightPdf *= selectionProbability;
    float weight = balanceHeuristic(payload.brdfPdf, lightPdf);
    payload.color = emitter.data.color * weight;
}
