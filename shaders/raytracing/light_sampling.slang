#ifndef RENDERLAB_LIGHT_SAMPLING_SLANG
#define RENDERLAB_LIGHT_SAMPLING_SLANG

#include "math/color.slang"
#include "path_tracing.slang"

static const uint kInvalidLightSample     = 0;
static const uint kAnalyticLightSample    = 1;
static const uint kTriangleLightSample    = 2;
#if HAS_ENV
static const uint kEnvironmentLightSample = 3;
#endif

struct PathLightSample
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

uint analyticLightCount()
{
    LightRanges ranges = sceneGlobals[0].lightRanges;
    return ranges.pointEnd + ranges.spotCount;
}

uint lightGroupCount()
{
    return uint(analyticLightCount() > 0) + uint(triangleLightCount() > 0) + uint(HAS_ENV);
}

float lightSelectionPdf(PathLightSample sample)
{
    uint count = sample.kind == kTriangleLightSample ? triangleLightCount()
        : sample.kind == kAnalyticLightSample        ? analyticLightCount()
                                                     : 1;
    return 1.0 / (float(lightGroupCount()) * float(count));
}

PathLightSample sampleLight(inout uint state)
{
    PathLightSample result = {};
    uint                groups = lightGroupCount();
    if (groups == 0)
    {
        return result;
    }

    uint group         = uniformLightIndex(groups, state);
    uint analyticCount = analyticLightCount();
    uint triangleCount = triangleLightCount();
    if (analyticCount > 0)
    {
        if (group == 0)
        {
            result.kind  = kAnalyticLightSample;
            uint offset = uniformLightIndex(analyticCount, state);
            LightRanges ranges = sceneGlobals[0].lightRanges;
            result.index = offset < ranges.pointEnd ? offset : offset + ranges.rectAreaEnd - ranges.pointEnd;
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
#if HAS_ENV
    if (result.kind == kInvalidLightSample)
    {
        result.kind = kEnvironmentLightSample;
    }
#endif

    result.uv.x = randomFloat(state);
    result.uv.y = randomFloat(state);
    return result;
}

struct PathLightEvaluation
{
    float3 contribution;  // Integrand over discrete light ID and uniform unit-square sample coordinates.
    float3 direction;
    float  distance;
    float  conditionalPdf;  // Conditional solid-angle density; 1 for delta lights.
    uint   delta;

    property float target
    {
        get { return rec709Luminance(contribution); }
    }
};

PathLightEvaluation evaluateLightSample(Surface surface, PathLightSample sample)
{
    PathLightEvaluation result        = {};
    float3                  origin        = surface.rayOrigin();
    float3                  emission      = float3(0.0);

    if (sample.kind == kTriangleLightSample)
    {
        LightTriangle triangle = loadTriangleLight(sample.index);
        if (triangle.area <= 0.0)
            return result;

        float2 uv;
        float3 offset = sampleTriangle(sample.index, sample.uv, uv) - origin;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0)
            return result;

        result.distance = sqrt(distanceSquared);
        result.direction = offset / result.distance;
        float cosine = dot(triangle.geometricNormal, -result.direction);
        cosine = triangle.twoSided != 0 ? abs(cosine) : max(0.0, cosine);
        if (cosine <= 0.0)
            return result;

        result.conditionalPdf = distanceSquared / (triangle.area * cosine);
        emission = emissionRgb(areaEmissions[triangle.emissionIndex], uv);
        result.contribution = surface.evaluateLight(result.direction, emission) / result.conditionalPdf;
        return result;
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

        float3 offset          = light.position - origin;
        float  distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0 || !withinLightRange(light, distanceSquared))
            return result;

        result.distance   = sqrt(distanceSquared);
        result.direction  = offset / result.distance;
        float attenuation = light.type == LightType::Spot ? spotAngularAttenuation(light, -result.direction) : 1.0;
        result.conditionalPdf = 1.0;
        result.delta          = 1;
        result.contribution   = surface.evaluateLight(result.direction, emission * (attenuation / distanceSquared));
        return result;
    }
#if HAS_ENV
    else if (sample.kind == kEnvironmentLightSample)
    {
        float elevation       = 1.0 - 2.0 * sample.uv.x;
        float radius          = sqrt(max(0.0, 1.0 - elevation * elevation));
        float angle           = 2.0 * PI * sample.uv.y;
        result.direction      = float3(radius * cos(angle), elevation, radius * sin(angle));
        result.distance       = kMaxRayDistance;
        result.conditionalPdf = 1.0 / (4.0 * PI);
        emission              = environmentRadiance(result.direction);
        result.contribution   = surface.evaluateLight(result.direction, emission) / result.conditionalPdf;
        return result;
    }
#endif
    return result;
}

PathLightSample lightSampleAtHit(RayDesc ray, PathHit hit)
{
    PathLightSample result = {};
    if (hit.event == PathEvent::Emitter ||
        (hit.event == PathEvent::Surface && hit.surface.emissiveTriangleIndex != kInvalidLightIndex))
    {
        result.kind  = kTriangleLightSample;
        result.index = hit.event == PathEvent::Emitter ? hit.lightIndex : hit.surface.emissiveTriangleIndex;
        float root = hit.barycentrics.x + hit.barycentrics.y;
        result.uv = float2(root * root, root > 0.0 ? hit.barycentrics.y / root : 0.0);
    }
#if HAS_ENV
    else if (hit.event == PathEvent::Miss)
    {
        result.kind = kEnvironmentLightSample;
        result.uv   = float2((1.0 - ray.Direction.y) * 0.5, frac(atan2(ray.Direction.z, ray.Direction.x) / (2.0 * PI)));
    }
#endif
    return result;
}

#endif
