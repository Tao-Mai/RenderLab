#ifndef RENDERLAB_REALTIME_LIGHT_SAMPLING_SLANG
#define RENDERLAB_REALTIME_LIGHT_SAMPLING_SLANG

#include "passes/raytracing/realtime/trace/ray_tracing.slang"

static const uint kInvalidLightSample     = 0;
static const uint kAnalyticLightSample    = 1;
static const uint kTriangleLightSample    = 2;
static const uint kEnvironmentLightSample = 3;

static const float3 kLightLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

struct RealtimeLightSample
{
    uint   kind;
    uint   index;
    float2 uv;
};

uint uniformLightIndex(uint count, inout uint state)
{
    uint threshold = (0u - count) % count;
    uint value;
    do
    {
        value = randomUint(state);
    }
    while (value < threshold);
    return value % count;
}

uint lightGroupCount()
{
    return uint(parameters.lightRanges.spotEnd > 0) + uint(emissiveTriangles.Load(0) > 0) +
        uint(parameters.iblEnabled != 0);
}

float lightSelectionPdf(RealtimeLightSample sample)
{
    uint count = sample.kind == kTriangleLightSample ? emissiveTriangles.Load(0)
        : sample.kind == kAnalyticLightSample        ? parameters.lightRanges.spotEnd
                                                     : 1;
    return 1.0 / (float(lightGroupCount()) * float(count));
}

RealtimeLightSample sampleLight(inout uint state)
{
    RealtimeLightSample result = {};
    uint                groups = lightGroupCount();
    if (groups == 0)
    {
        return result;
    }

    uint group         = uniformLightIndex(groups, state);
    uint analyticCount = parameters.lightRanges.spotEnd;
    uint triangleCount = emissiveTriangles.Load(0);
    if (analyticCount > 0)
    {
        if (group == 0)
        {
            result.kind  = kAnalyticLightSample;
            result.index = uniformLightIndex(analyticCount, state);
        }
        else
        {
            --group;
        }
    }
    if (result.kind == kInvalidLightSample && triangleCount > 0)
    {
        if (group == 0)
        {
            result.kind  = kTriangleLightSample;
            result.index = uniformLightIndex(triangleCount, state);
        }
        else
        {
            --group;
        }
    }
    if (result.kind == kInvalidLightSample)
    {
        result.kind = kEnvironmentLightSample;
    }

    result.uv.x = randomFloat(state);
    result.uv.y = randomFloat(state);
    return result;
}

struct RealtimeLightEvaluation
{
    float3 contribution;  // Integrand over discrete light ID and uniform unit-square sample coordinates.
    float3 direction;
    float  distance;
    float  conditionalPdf;  // Conditional solid-angle density; 1 for delta lights.
    uint   delta;

    property float target
    {
        get { return dot(contribution, kLightLuminanceWeights); }
    }
};

RealtimeLightEvaluation evaluateLightSample(Surface surface, RealtimeLightSample sample)
{
    RealtimeLightEvaluation result        = {};
    float3                  origin        = surface.rayOrigin();
    float3                  emission      = float3(0.0);
    float3                  lightPosition = float3(0.0);
    float3                  lightNormal   = float3(0.0);
    float                   area          = 0.0;
    bool                    twoSided      = false;

    if (sample.kind == kTriangleLightSample)
    {
        EmissiveTriangle triangle = loadEmissiveTriangle(sample.index);
        if (triangle.area <= 0.0)
        {
            return result;
        }

        float3 barycentrics = triangleBarycentrics(sample.uv);
        lightPosition       = triangle.position + triangle.edge1 * barycentrics.y + triangle.edge2 * barycentrics.z;
        lightNormal         = cross(triangle.edge1, triangle.edge2) / (2.0 * triangle.area);
        area                = triangle.area;
        twoSided            = materials[triangle.materialIndex].doubleSided != 0;
        emission            = triangleLightEmission(triangle, barycentrics);
    }
    else if (sample.kind == kAnalyticLightSample)
    {
        LightData light = lights[sample.index];
        emission        = light.color;
        if (light.type == LightType::Directional)
        {
            result.direction      = -normalize(light.direction.xyz);
            result.distance       = kMaxRayDistance;
            result.conditionalPdf = 1.0;
            result.delta          = 1;
            result.contribution   = surface.evaluateLight(result.direction, emission);
            return result;
        }

        if (light.type == LightType::RectArea)
        {
            float3 up     = normalize(cross(light.direction.xyz, light.areaRight));
            lightPosition = light.position + light.areaRight * ((sample.uv.x - 0.5) * light.areaSize.x) +
                up * ((sample.uv.y - 0.5) * light.areaSize.y);
            lightNormal = normalize(light.direction.xyz);
            area        = light.areaSize.x * light.areaSize.y;
        }
        else
        {
            float3 offset          = light.position - origin;
            float  distanceSquared = dot(offset, offset);
            if (distanceSquared == 0.0 || distanceSquared > light.range * light.range)
            {
                return result;
            }

            result.distance   = sqrt(distanceSquared);
            result.direction  = offset / result.distance;
            float attenuation = light.type == LightType::Spot ? spotAngularAttenuation(light, -result.direction) : 1.0;
            result.conditionalPdf = 1.0;
            result.delta          = 1;
            result.contribution   = surface.evaluateLight(result.direction, emission * (attenuation / distanceSquared));
            return result;
        }
    }
    else if (sample.kind == kEnvironmentLightSample)
    {
        float elevation       = 1.0 - 2.0 * sample.uv.x;
        float radius          = sqrt(max(0.0, 1.0 - elevation * elevation));
        float angle           = 2.0 * PI * sample.uv.y;
        result.direction      = float3(radius * cos(angle), elevation, radius * sin(angle));
        result.distance       = kMaxRayDistance;
        result.conditionalPdf = 1.0 / (4.0 * PI);
        emission              = radianceMap.SampleLevel(radianceSampler, result.direction, 0.0).rgb;
        result.contribution   = surface.evaluateLight(result.direction, emission) / result.conditionalPdf;
        return result;
    }
    else
    {
        return result;
    }

    float3 offset          = lightPosition - origin;
    float  distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0 || area <= 0.0)
    {
        return result;
    }

    result.distance  = sqrt(distanceSquared);
    result.direction = offset / result.distance;
    float cosine     = dot(lightNormal, -result.direction);
    cosine           = twoSided ? abs(cosine) : max(0.0, cosine);
    if (cosine <= 0.0)
    {
        return result;
    }

    result.conditionalPdf = distanceSquared / (area * cosine);
    result.contribution   = surface.evaluateLight(result.direction, emission) / result.conditionalPdf;
    return result;
}

RealtimeLightSample lightSampleAtHit(RayDesc ray, PathHit hit)
{
    RealtimeLightSample result = {};
    if (hit.event == PathEvent::Surface && hit.surface.emissiveTriangleIndex != kInvalidEmissiveTriangle)
    {
        result.kind  = kTriangleLightSample;
        result.index = hit.surface.emissiveTriangleIndex;
        result.uv    = triangleSampleUv(hit.barycentrics);
    }
    else if (hit.event == PathEvent::Emitter)
    {
        result.kind      = kAnalyticLightSample;
        result.index     = hit.lightIndex;
        LightData light  = lights[hit.lightIndex];
        float3    up     = normalize(cross(light.direction.xyz, light.areaRight));
        float3    offset = ray.Origin + ray.Direction * hit.distance - light.position;
        result.uv        = float2(dot(offset, light.areaRight), dot(offset, up)) / light.areaSize + 0.5;
    }
    else if (hit.event == PathEvent::Miss && parameters.iblEnabled != 0)
    {
        result.kind = kEnvironmentLightSample;
        result.uv   = float2((1.0 - ray.Direction.y) * 0.5, frac(atan2(ray.Direction.z, ray.Direction.x) / (2.0 * PI)));
    }
    return result;
}

#endif
