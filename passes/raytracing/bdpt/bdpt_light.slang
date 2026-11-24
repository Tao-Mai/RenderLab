#include "raytracing/analytic_light_sampling.slang"

StructuredBuffer<float> lightDistribution;

float2 bdptRandom2D(inout uint state)
{
    float2 value;
    value.x = randomFloat(state);
    value.y = randomFloat(state);

    return value;
}

float3 bdptFrameDirection(float3 normal, float3 local)
{
    float3 axis = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 tangent = normalize(cross(axis, normal));

    return tangent * local.x + cross(normal, tangent) * local.y + normal * local.z;
}

float3 bdptUniformSphere(float2 u)
{
    float z = 1.0 - 2.0 * u.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PI * u.y;

    return float3(r * cos(phi), r * sin(phi), z);
}

uint bdptLightCount()
{
    return scene.globals[0].analyticLightCount + emissiveLights.triangleCount + uint(HAS_ENV);
}

uint bdptLightLeafBase(uint count)
{
    return count <= 1 ? 1u : 2u << firstbithigh(count - 1);
}

BdptLightReference bdptLightAt(uint index)
{
    uint analyticCount = scene.globals[0].analyticLightCount;
    if (index < analyticCount)
    {
        LightType type = scene.analyticLights[index].type;
        if (type == LightType::Point)
            return {BdptLightType::Point, index};
        if (type == LightType::Spot)
            return {BdptLightType::Spot, index};
        if (type == LightType::RectArea)
            return {BdptLightType::Rectangle, index};

        return {BdptLightType::Directional, index};
    }
    index -= analyticCount;
    if (index < emissiveLights.triangleCount)
    {
        return {BdptLightType::Emissive, index};
    }

    return {BdptLightType::Environment, 0};
}

uint bdptLightIndex(BdptLightReference light)
{
    if (light.type == BdptLightType::Emissive)
        return scene.globals[0].analyticLightCount + light.index;
    if (light.type == BdptLightType::Environment)
        return bdptLightCount() - 1;

    return light.index;
}

float bdptLightPmf(BdptLightReference light)
{
    float total = lightDistribution[1];
    if (light.type == BdptLightType::None || total <= 0.0)
        return 0.0;
    return lightDistribution[bdptLightLeafBase(bdptLightCount()) + bdptLightIndex(light)] / total;
}

BdptLightReference bdptSelectLight(inout uint state)
{
    uint count = bdptLightCount();
    float total = lightDistribution[1];
    if (count == 0 || total <= 0.0)
    {
        return {BdptLightType::None, 0};
    }

    uint leaves = bdptLightLeafBase(count);
    uint node = 1;
    float value = randomFloat(state) * total;
    while (node < leaves)
    {
        uint left = node * 2;
        float leftWeight = lightDistribution[left];
        if (value < leftWeight || lightDistribution[left + 1] == 0.0)
            node = left;
        else
        {
            node = left + 1;
            value -= leftWeight;
        }
    }
    return bdptLightAt(node - leaves);
}

bool bdptLightHittable(BdptLightReference light)
{
    return light.type == BdptLightType::Emissive || light.type == BdptLightType::Environment;
}

LightData bdptAnalyticLight(BdptLightReference light)
{
    return scene.analyticLights[light.index];
}

float bdptEmissionDirectionPdf(BdptVertex vertex, float3 direction)
{
    if (vertex.light.type == BdptLightType::Emissive)
    {
        EmissiveTriangle triangle = emissiveLights.triangles[vertex.light.index];
        float cosine = dot(triangle.geometricNormal, direction);
        if (emissiveTwoSided(triangle))
        {
            return abs(cosine) * (0.5 * INV_PI);
        }

        return max(0.0, cosine) * INV_PI;
    }
    if (vertex.light.type == BdptLightType::Rectangle)
        return max(0.0, dot(vertex.geometricNormal, direction)) * INV_PI;
    if (vertex.light.type == BdptLightType::Point)
    {
        return 1.0 / (4.0 * PI);
    }
    if (vertex.light.type == BdptLightType::Spot)
    {
        LightData light = bdptAnalyticLight(vertex.light);

        return dot(light.direction.xyz, direction) >= light.cosOuter
            ? 1.0 / (2.0 * PI * (1.0 - light.cosOuter)) : 0.0;
    }

    return 0.0;
}

float bdptLightOriginPdf(BdptVertex vertex)
{
    float probability = bdptLightPmf(vertex.light);
    if (vertex.isInfinite())
    {
        return vertex.light.type == BdptLightType::Environment ? probability / (4.0 * PI) : 0.0;
    }
    if (vertex.light.type == BdptLightType::Emissive)
    {
        float area = emissiveLights.triangles[vertex.light.index].area;

        return area > 0.0 ? probability / area : 0.0;
    }
    if (vertex.light.type == BdptLightType::Rectangle)
        return probability / rectLightArea(bdptAnalyticLight(vertex.light));

    return probability;
}

float bdptLightPdf(BdptVertex light, BdptVertex next)
{
    float3 direction = bdptDirection(light, next);
    float pdf;
    if (light.isInfinite())
    {
        pdf = 1.0 / (PI * parameters.sceneRadius * parameters.sceneRadius);
    }
    else
    {
        float3 offset = next.position - light.position;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0 ||
            ((light.light.type == BdptLightType::Point || light.light.type == BdptLightType::Spot) &&
             !withinLightRange(bdptAnalyticLight(light.light), distanceSquared)))
        {
            return 0.0;
        }
        pdf = bdptEmissionDirectionPdf(light, direction) / distanceSquared;
    }
    if (next.isOnSurface())
    {
        pdf *= abs(dot(next.geometricNormal, direction));
    }

    return pdf;
}

// Sample one incident light, independently of the emitted light subpath.
bool bdptSampleLi(BdptVertex reference, inout uint state, out BdptVertex sampled)
{
    sampled = {};
    sampled.type = BdptVertexType::Light;
    sampled.light = bdptSelectLight(state);
    float choicePdf = bdptLightPmf(sampled.light);
    if (choicePdf == 0.0)
    {
        return false;
    }

    float3 radiance;
    float pdf = 1.0;
    float3 direction;
    if (sampled.light.type == BdptLightType::Emissive)
    {
        sampled.position = sampleEmissiveTriangle(sampled.light.index, bdptRandom2D(state), sampled.uv);
        EmissiveTriangle triangle = emissiveLights.triangles[sampled.light.index];
        sampled.normal = sampled.geometricNormal = triangle.geometricNormal;
        float3 offset = sampled.position - reference.position;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0 || triangle.area == 0.0)
        {
            return false;
        }
        direction = offset * rsqrt(distanceSquared);
        float cosine = abs(dot(triangle.geometricNormal, direction));
        if (cosine == 0.0)
        {
            return false;
        }
        pdf = distanceSquared / (triangle.area * cosine);
        radiance = emissiveRadiance(sampled.light.index, sampled.uv, -direction);
    }
    else if (sampled.light.type == BdptLightType::Environment)
    {
        direction = bdptUniformSphere(bdptRandom2D(state));
        pdf = 1.0 / (4.0 * PI);
        radiance = environmentRadiance(direction);
        sampled.wo = direction;
        sampled.position = reference.position + direction * (2.0 * parameters.sceneRadius);
    }
    else
    {
        LightData light = bdptAnalyticLight(sampled.light);
        AnalyticLightSample analytic;
        if (!sampleAnalyticLight(light, reference.position, bdptRandom2D(state), analytic))
            return false;
        direction = analytic.direction;
        radiance = analytic.radiance;
        pdf = analytic.pdf;
        sampled.position = analytic.position;
        if (sampled.light.type == BdptLightType::Directional)
        {
            sampled.wo = direction;
            sampled.position = reference.position + direction * (2.0 * parameters.sceneRadius);
        }
        if (sampled.light.type == BdptLightType::Rectangle)
            sampled.normal = sampled.geometricNormal = light.direction.xyz;
    }

    sampled.beta = radiance / (choicePdf * pdf);
    sampled.pdfFwd = bdptLightOriginPdf(sampled);

    return any(radiance > 0.0);
}

bool bdptSampleLe(inout uint state, out BdptVertex endpoint, out RayDesc ray,
    out float3 beta, out float directionPdf)
{
    endpoint = {};
    endpoint.type = BdptVertexType::Light;
    endpoint.light = bdptSelectLight(state);
    ray = {};
    beta = float3(0.0);
    directionPdf = 0.0;
    float choicePdf = bdptLightPmf(endpoint.light);
    if (choicePdf == 0.0)
    {
        return false;
    }

    float2 positionSample = bdptRandom2D(state);
    float2 directionSample = bdptRandom2D(state);
    float positionPdf = 1.0;
    float3 radiance;
    float cosine = 1.0;
    if (endpoint.light.type == BdptLightType::Emissive || endpoint.light.type == BdptLightType::Rectangle)
    {
        bool twoSided = false;
        bool mesh = endpoint.light.type == BdptLightType::Emissive;
        if (mesh)
        {
            EmissiveTriangle triangle = emissiveLights.triangles[endpoint.light.index];
            if (triangle.area == 0.0)
                return false;
            endpoint.position = sampleEmissiveTriangle(endpoint.light.index, positionSample, endpoint.uv);
            endpoint.normal = endpoint.geometricNormal = triangle.geometricNormal;
            positionPdf = 1.0 / triangle.area;
            twoSided = emissiveTwoSided(triangle);
        }
        else
        {
            LightData light = bdptAnalyticLight(endpoint.light);
            endpoint.position = sampleRectLightPosition(light, positionSample);
            endpoint.normal = endpoint.geometricNormal = light.direction.xyz;
            positionPdf = 1.0 / rectLightArea(light);
        }

        float side = 1.0;
        if (twoSided)
        {
            side = directionSample.x < 0.5 ? 1.0 : -1.0;
            directionSample.x = frac(directionSample.x * 2.0);
        }

        float radius = sqrt(directionSample.x);
        float phi = 2.0 * PI * directionSample.y;
        float3 local = float3(radius * cos(phi), radius * sin(phi), sqrt(1.0 - directionSample.x));
        ray.Direction = bdptFrameDirection(endpoint.geometricNormal * side, local);
        cosine = local.z;
        directionPdf = cosine * INV_PI * (twoSided ? 0.5 : 1.0);
        radiance = mesh ? emissiveRadiance(endpoint.light.index, endpoint.uv, ray.Direction)
            : bdptAnalyticLight(endpoint.light).color;
    }
    else if (endpoint.light.type == BdptLightType::Point)
    {
        LightData light = bdptAnalyticLight(endpoint.light);
        endpoint.position = light.position;
        ray.Direction = bdptUniformSphere(directionSample);
        directionPdf = 1.0 / (4.0 * PI);
        radiance = light.color;
    }
    else if (endpoint.light.type == BdptLightType::Spot)
    {
        LightData light = bdptAnalyticLight(endpoint.light);
        endpoint.position = light.position;
        float z = lerp(1.0, light.cosOuter, directionSample.x);
        float radius = sqrt(max(0.0, 1.0 - z * z));
        float phi = 2.0 * PI * directionSample.y;
        ray.Direction = bdptFrameDirection(light.direction.xyz, float3(radius * cos(phi), radius * sin(phi), z));
        directionPdf = 1.0 / (2.0 * PI * (1.0 - light.cosOuter));
        radiance = light.color * spotAngularAttenuation(light, ray.Direction);
    }
    else
    {
        if (endpoint.light.type == BdptLightType::Directional)
        {
            LightData light = bdptAnalyticLight(endpoint.light);
            ray.Direction = light.direction.xyz;
            directionPdf = 1.0;
            radiance = light.color;
        }
        else
        {
            ray.Direction = bdptUniformSphere(directionSample);
            directionPdf = 1.0 / (4.0 * PI);
            radiance = environmentRadiance(-ray.Direction);
        }

        endpoint.wo = -ray.Direction;
        float radius = parameters.sceneRadius * sqrt(positionSample.x);
        float phi = 2.0 * PI * positionSample.y;
        float3 disk = bdptFrameDirection(ray.Direction, float3(radius * cos(phi), radius * sin(phi), 0.0));
        endpoint.position = parameters.sceneCenter - ray.Direction * parameters.sceneRadius + disk;
        positionPdf = 1.0 / (PI * parameters.sceneRadius * parameters.sceneRadius);
    }

    if (directionPdf == 0.0 || !any(radiance > 0.0))
    {
        return false;
    }

    endpoint.beta = radiance;
    endpoint.pdfFwd = choicePdf * positionPdf;
    beta = radiance * (cosine / (choicePdf * positionPdf * directionPdf));

    ray.Origin = bdptOffset(endpoint, ray.Direction);
    ray.TMin = kRayEpsilon;
    ray.TMax = kMaxRayDistance;

    return true;
}
