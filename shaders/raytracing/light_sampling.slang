#ifndef RENDERLAB_LIGHT_SAMPLING_SLANG
#define RENDERLAB_LIGHT_SAMPLING_SLANG

#include "math/color.slang"
#include "path_tracing.slang"
#include "analytic_light_sampling.slang"
#include "raytracing/alpha_test.slang"

static const uint kInvalidLightSample     = 0;
static const uint kAnalyticLightSample    = 1;
static const uint kEmissiveLightSample    = 2;
#if HAS_ENV
static const uint kEnvironmentLightSample = 3;
#endif

struct PathLightSample
{
    uint   kind;
    uint   index;
    float2 uv;
};

uint analyticLightCount()
{
#if defined(USE_ANALYTIC_LIGHTS) && !USE_ANALYTIC_LIGHTS
    return 0;
#else
    return scene.globals[0].analyticLightCount;
#endif
}

uint emissiveLightCount()
{
#if defined(USE_EMISSIVE_LIGHTS) && !USE_EMISSIVE_LIGHTS
    return 0;
#else
    return emissiveLights.triangleCount;
#endif
}

uint lightGroupCount()
{
    return uint(analyticLightCount() > 0) + uint(emissiveLightCount() > 0) + uint(HAS_ENV);
}

float lightSelectionPdf(PathLightSample sample)
{
    uint count = sample.kind == kEmissiveLightSample ? emissiveLightCount()
        : sample.kind == kAnalyticLightSample        ? analyticLightCount()
                                                      : 1;
    return 1.0 / (float(lightGroupCount()) * float(count));
}

PathLightSample sampleLight(inout SampleGenerator state)
{
    PathLightSample result = {};
    uint                groups = lightGroupCount();
    if (groups == 0)
    {
        return result;
    }

    uint group         = uniformLightIndex(groups, state);
    uint analyticCount = analyticLightCount();
    uint triangleCount = emissiveLightCount();
    if (analyticCount > 0)
    {
        if (group == 0)
        {
            result.kind = kAnalyticLightSample;
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
            result.kind  = kEmissiveLightSample;
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

    property float target
    {
        get { return rec709Luminance(contribution); }
    }
};

PathLightEvaluation evaluateLightSample(Surface surface, PathLightSample sample, bool testEmissiveAlpha = false)
{
    PathLightEvaluation result        = {};
    float3                  origin        = surface.rayOrigin();
    float3                  emission      = float3(0.0);

    if (sample.kind == kEmissiveLightSample)
    {
        EmissiveTriangle triangle = emissiveLights.triangles[sample.index];
        if (triangle.area <= 0.0)
            return result;

        float2 uv;
        float3 offset = sampleEmissiveTriangle(sample.index, sample.uv, uv) - origin;
        if (testEmissiveAlpha)
        {
            MaterialData material = materials[triangle.materialIndex];
            if (material.alphaMode == kAlphaModeMask && failsAlphaCutoff(material, uv))
                return result;
        }

        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0)
            return result;

        result.distance = sqrt(distanceSquared);
        result.direction = offset / result.distance;
        float cosine = dot(triangle.geometricNormal, -result.direction);
        cosine = emissiveTwoSided(triangle) ? abs(cosine) : max(0.0, cosine);
        if (cosine <= 0.0)
            return result;

        result.conditionalPdf = distanceSquared / (triangle.area * cosine);
        emission = emissiveRadiance(sample.index, uv, -result.direction);
        result.contribution = surface.evaluateLight(result.direction, emission) / result.conditionalPdf;
        return result;
    }
    else if (sample.kind == kAnalyticLightSample)
    {
        AnalyticLightSample analytic;
        if (!sampleAnalyticLight(scene.analyticLights[sample.index], origin, sample.uv, analytic))
            return result;

        result.distance = analytic.distance;
        result.direction = analytic.direction;
        result.conditionalPdf = analytic.pdf;
        result.contribution = surface.evaluateLight(analytic.direction, analytic.radiance) / analytic.pdf;
        return result;
    }
#if HAS_ENV
    else if (sample.kind == kEnvironmentLightSample)
    {
        result.direction      = sampleEnvironmentDirection(sample.uv);
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
    if (hit.event == PathEvent::Surface && hit.surface.emissiveTriangleIndex != kInvalidLightIndex)
    {
        result.kind  = kEmissiveLightSample;
        result.index = hit.surface.emissiveTriangleIndex;
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
