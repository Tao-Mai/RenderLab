#ifndef RENDERLAB_OFFLINE_BDPT_SLANG
#define RENDERLAB_OFFLINE_BDPT_SLANG

#include "raytracing/offline/path_common.slang"
#include "raytracing/offline/bdpt_bsdf.slang"
#include "raytracing/offline/bdpt_light.slang"

static const uint kBdptVertexCapacity = BDPT_MAX_DEPTH + 2;

bool bdptProjectCamera(float3 direction, out float2 pixel)
{
    pixel = float2(0.0);
    if (dot(parameters.cameraForward, direction) <= 0.0)
    {
        return false;
    }

    float4 clip = mul(sceneUniforms.viewProjection,
        float4(sceneUniforms.cameraPosition.xyz + direction, 1.0));
    float2 ndc = clip.xy / clip.w;
    if (any(ndc < -1.0) || any(ndc >= 1.0))
    {
        return false;
    }
    pixel = parameters.viewportOffset + (ndc * 0.5 + 0.5) * parameters.viewportSize;

    return true;
}

float bdptCameraPdf(float3 direction)
{
    float2 pixel;
    if (!bdptProjectCamera(direction, pixel))
    {
        return 0.0;
    }

    float cosine = dot(parameters.cameraForward, direction);

    return 1.0 / (parameters.imagePlaneArea * cosine * cosine * cosine);
}

BdptVertex bdptCameraVertex()
{
    BdptVertex camera = {};
    camera.type = BdptVertexType::Camera;
    camera.position = sceneUniforms.cameraPosition.xyz;
    camera.normal = parameters.cameraForward;
    camera.beta = float3(1.0);

    return camera;
}

float bdptVertexPdf(BdptVertex vertex, BdptVertex previous, BdptVertex next)
{
    if (vertex.type == BdptVertexType::Light)
    {
        return bdptLightPdf(vertex, next);
    }

    float3 direction = bdptDirection(vertex, next);
    float pdf;
    if (vertex.type == BdptVertexType::Camera)
    {
        pdf = bdptCameraPdf(direction);
    }
    else
    {
        BdptBsdf bsdf = bdptBsdf(vertex, bdptDirection(vertex, previous));
        pdf = bsdf.pdf(direction);
    }

    return bdptConvertDensity(vertex, pdf, next);
}

uint bdptRandomWalk(__ref BdptVertex path[kBdptVertexCapacity], RayDesc ray, float3 beta, float pdfFwd,
    uint maxVertices, TransportMode mode, inout uint state)
{
    uint count = 1;
    for (uint index = 1; index < maxVertices; ++index)
    {
        Hit hit = {};
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, kSceneMask, 0, 0, 0, ray, hit);

        BdptVertex vertex = {};
        vertex.beta = beta;
        vertex.wo = -ray.Direction;
        if (hit.hit == 0)
        {
            if (mode == TransportMode::Radiance && parameters.hasEnvironmentLight)
            {
                vertex.type = BdptVertexType::Light;
                vertex.light = {BdptLightType::Environment, 0};
                vertex.wo = ray.Direction;
                vertex.position = ray.Origin + ray.Direction * (2.0 * parameters.sceneRadius);
                vertex.pdfFwd = pdfFwd;
                path[index] = vertex;
                ++count;
            }
            break;
        }

        HitData object = objects[hit.objectIndex];
        vertex.type = BdptVertexType::Surface;
        vertex.position = hit.position;
        vertex.normal = hit.normal;
        vertex.geometricNormal = hit.geometricNormal;
        vertex.tangent = hit.tangent;
        vertex.uv = hit.uv;

        bdptPrepareMaterial(vertex, object.materialIndex, hit.primitiveIndex);
        if (object.firstAreaLight != kOfflineInvalidIndex)
        {
            vertex.light = {BdptLightType::Area, object.firstAreaLight + hit.primitiveIndex};
        }

        vertex.pdfFwd = bdptConvertDensity(path[index - 1], pdfFwd, vertex);
        path[index] = vertex;
        ++count;
        if (count == maxVertices)
        {
            break;
        }

        BdptBsdf bsdf = bdptBsdf(vertex, vertex.wo);
        float branchSample = randomFloat(state);
        float2 directionSample = bdptRandom2D(state);
        BrdfSample sampled = bsdf.sample(branchSample, directionSample, mode);
        if (!any(sampled.weight > 0.0) || (sampled.delta == 0 && sampled.pdf == 0.0))
        {
            break;
        }

        beta *= sampled.weight;
        pdfFwd = sampled.pdf;

        BdptBsdf reverseBsdf = bdptBsdf(vertex, sampled.direction);
        float pdfRev = reverseBsdf.pdf(vertex.wo);
        if (sampled.delta != 0)
        {
            path[index].delta = 1;
            pdfFwd = pdfRev = 0.0;
        }

        path[index - 1].pdfRev = bdptConvertDensity(vertex, pdfRev, path[index - 1]);

        ray.Origin = bdptOffset(vertex, sampled.direction);
        ray.Direction = sampled.direction;
        ray.TMin = 0.0;
        ray.TMax = kMaxRayDistance;
    }

    return count;
}

uint bdptCameraSubpath(RayDesc ray, inout uint state, __ref BdptVertex path[kBdptVertexCapacity])
{
    path[0] = bdptCameraVertex();

    return bdptRandomWalk(path, ray, float3(1.0), bdptCameraPdf(ray.Direction), parameters.maxBounces + 2,
        TransportMode::Radiance, state);
}

uint bdptLightSubpath(inout uint state, __ref BdptVertex path[kBdptVertexCapacity])
{
    RayDesc ray;
    float3 beta;
    float directionPdf;
    if (!bdptSampleLe(state, path[0], ray, beta, directionPdf))
    {
        return 0;
    }

    uint count = bdptRandomWalk(path, ray, beta, directionPdf, parameters.maxBounces + 1,
        TransportMode::Importance, state);
    if (path[0].isInfinite())
    {
        if (count > 1)
        {
            path[1].pdfFwd = abs(dot(path[1].geometricNormal, ray.Direction))
                / (PI * parameters.sceneRadius * parameters.sceneRadius);
        }
        path[0].pdfFwd = bdptLightOriginPdf(path[0]);
    }

    return count;
}

float bdptLogRatio(float numerator, float denominator)
{
    // Specular densities are Dirac masses, recorded as zero in both measures.
    return log(numerator != 0.0 ? numerator : 1.0) - log(denominator != 0.0 ? denominator : 1.0);
}

void bdptAddLogWeight(inout float sumLog, float ratioLog)
{
    float exponent = parameters.powerHeuristic ? 2.0 : 1.0;
    float term = exponent * ratioLog;
    float largest = max(sumLog, term);
    sumLog = largest + log(exp(sumLog - largest) + exp(term - largest));
}

// Enumerate the probability ratios of every valid split of this same path.
// Only the two connection vertices and their predecessors need new reverse PDFs.
float bdptMisWeight(__constref BdptVertex light[kBdptVertexCapacity], __constref BdptVertex camera[kBdptVertexCapacity],
    BdptVertex sampled, uint s, uint t)
{
    if (s + t == 2)
    {
        return 1.0;
    }

    BdptVertex qs = {};
    BdptVertex qsMinus = {};
    BdptVertex pt = camera[t - 1];
    BdptVertex ptMinus = {};
    if (s > 0)
    {
        qs = s == 1 ? sampled : light[s - 1];
    }
    if (s > 1)
    {
        qsMinus = light[s - 2];
    }
    if (t > 1)
    {
        ptMinus = camera[t - 2];
    }
    else
    {
        pt = sampled;
    }

    float ptRev = s > 0 ? bdptVertexPdf(qs, qsMinus, pt) : bdptLightOriginPdf(pt);
    float ptMinusRev = 0.0;
    if (t > 1)
    {
        ptMinusRev = s > 0 ? bdptVertexPdf(pt, qs, ptMinus) : bdptLightPdf(pt, ptMinus);
    }

    float qsRev = 0.0;
    float qsMinusRev = 0.0;
    if (s > 0)
    {
        qsRev = bdptVertexPdf(pt, ptMinus, qs);
    }
    if (s > 1)
    {
        qsMinusRev = bdptVertexPdf(qs, pt, qsMinus);
    }

    float sumLog = 0.0;
    float ratioLog = 0.0;
    for (int index = int(t) - 1; index > 0; --index)
    {
        BdptVertex vertex = camera[index];
        float reversePdf = index == int(t) - 1 ? ptRev
            : (index == int(t) - 2 ? ptMinusRev : vertex.pdfRev);
        ratioLog += bdptLogRatio(reversePdf, vertex.pdfFwd);
        bool delta = index == int(t) - 1 ? false : vertex.delta != 0;
        if (!delta && camera[index - 1].delta == 0 && (index != 1 || parameters.lightTracing))
        {
            bdptAddLogWeight(sumLog, ratioLog);
        }
    }

    ratioLog = 0.0;
    for (int index = int(s) - 1; index >= 0; --index)
    {
        BdptVertex vertex = index == int(s) - 1 ? qs : light[index];
        float reversePdf = index == int(s) - 1 ? qsRev
            : (index == int(s) - 2 ? qsMinusRev : vertex.pdfRev);
        ratioLog += bdptLogRatio(reversePdf, vertex.pdfFwd);
        bool previousDelta = index > 0 ? light[index - 1].delta != 0 : bdptDeltaLight(vertex.light);
        bool delta = index == int(s) - 1 ? false : vertex.delta != 0;
        if (!delta && !previousDelta)
        {
            bdptAddLogWeight(sumLog, ratioLog);
        }
    }

    return exp(-sumLog);
}

bool bdptVisible(BdptVertex from, BdptVertex to)
{
    float3 direction = bdptDirection(from, to);
    float3 origin = bdptOffset(from, direction);
    if (to.isInfinite())
    {
        return visible(origin, direction, kMaxRayDistance);
    }

    float3 target = bdptOffset(to, -direction);
    float3 offset = target - origin;
    float distance = length(offset);
    if (distance <= kRayEpsilon)
    {
        return false;
    }

    return visible(origin, offset / distance, distance);
}

float3 bdptConnect(__constref BdptVertex light[kBdptVertexCapacity], __constref BdptVertex camera[kBdptVertexCapacity],
    uint s, uint t, inout uint state, out float2 pixel)
{
    pixel = float2(0.0);
    BdptVertex pt = camera[t - 1];
    BdptVertex sampled = {};
    float3 contribution = float3(0.0);
    if (s == 0)
    {
        if (pt.isLight())
        {
            float3 emission = pt.isInfinite() ? environmentEmission(true, pt.wo)
                : areaEmission(pt.light.index, pt.uv, pt.wo);
            contribution = pt.beta * emission;
        }
    }
    else if (pt.type != BdptVertexType::Light)
    {
        if (t == 1)
        {
            BdptVertex qs = light[s - 1];
            BdptBsdf bsdf = bdptBsdf(qs, qs.wo);
            float3 offset = qs.position - pt.position;
            float distanceSquared = dot(offset, offset);
            if (distanceSquared == 0.0)
            {
                return contribution;
            }

            float3 cameraDirection = offset * rsqrt(distanceSquared);
            if (!bsdf.connectible() || !bdptProjectCamera(cameraDirection, pixel))
            {
                return contribution;
            }
            sampled = bdptCameraVertex();
            // We / SampleWi.pdf = 1 / (A * cos(camera)^3 * distance^2).
            sampled.beta = float3(bdptCameraPdf(cameraDirection) / distanceSquared);
            contribution = qs.beta * bsdf.evaluate(-cameraDirection, TransportMode::Importance)
                * abs(dot(bsdf.normal, cameraDirection)) * sampled.beta;
            if (!bdptVisible(qs, sampled))
            {
                return float3(0.0);
            }
        }
        else if (s == 1)
        {
            BdptBsdf bsdf = bdptBsdf(pt, pt.wo);
            if (!bsdf.connectible() || !bdptSampleLi(pt, state, sampled))
            {
                return contribution;
            }

            float3 direction = bdptDirection(pt, sampled);
            contribution = pt.beta * bsdf.evaluate(direction, TransportMode::Radiance)
                * abs(dot(bsdf.normal, direction)) * sampled.beta;
            if (!bdptVisible(pt, sampled))
            {
                return float3(0.0);
            }
        }
        else
        {
            BdptVertex qs = light[s - 1];
            BdptBsdf cameraBsdf = bdptBsdf(pt, pt.wo);
            BdptBsdf lightBsdf = bdptBsdf(qs, qs.wo);
            if (!cameraBsdf.connectible() || !lightBsdf.connectible())
            {
                return contribution;
            }

            float3 offset = qs.position - pt.position;
            float distanceSquared = dot(offset, offset);
            if (distanceSquared == 0.0)
            {
                return contribution;
            }

            float3 direction = offset * rsqrt(distanceSquared);
            float geometry = abs(dot(cameraBsdf.normal, direction) * dot(lightBsdf.normal, direction))
                / distanceSquared;
            contribution = pt.beta * cameraBsdf.evaluate(direction, TransportMode::Radiance)
                * lightBsdf.evaluate(-direction, TransportMode::Importance) * qs.beta * geometry;
            if (!bdptVisible(pt, qs))
            {
                return float3(0.0);
            }
        }
    }
    if (!any(contribution > 0.0))
    {
        return float3(0.0);
    }

    return contribution * bdptMisWeight(light, camera, sampled, s, t);
}

void bdptSplat(float2 raster, float3 value)
{
    uint2 pixel = uint2(floor(raster));
    uint2 dimensions = parameters.imageSize;
    if (any(pixel >= dimensions))
    {
        return;
    }

    uint index = pixel.x + pixel.y * dimensions.x;

    // Float addition through uint CAS keeps this usable without atomic-float extensions.
    for (uint channel = 0; channel < 3; ++channel)
    {
        uint expected;
        InterlockedCompareExchange(splats[index][channel], 0u, 0u, expected);
        while (true)
        {
            uint observed;
            uint desired = asuint(asfloat(expected) + value[channel]);
            InterlockedCompareExchange(splats[index][channel], expected, desired, observed);
            if (observed == expected)
            {
                break;
            }
            expected = observed;
        }
    }
}

float3 tracePath(RayDesc ray, inout uint state)
{
    BdptVertex camera[kBdptVertexCapacity];
    BdptVertex light[kBdptVertexCapacity];
    uint cameraCount = bdptCameraSubpath(ray, state, camera);

    uint lightCount = bdptLightSubpath(state, light);

    uint maxS = max(lightCount, bdptLightCount() > 0 ? 1u : 0u);
    float3 radiance = float3(0.0);
    for (uint t = 1; t <= cameraCount; ++t)
    {
        for (uint s = 0; s <= maxS; ++s)
        {
            if (s + t < 2 || s + t - 2 > parameters.maxBounces ||
                (t == 1 && (s < 2 || !parameters.lightTracing)))
            {
                continue;
            }

            float2 pixel;
            float3 contribution = bdptConnect(light, camera, s, t, state, pixel);
            if (t == 1)
            {
                if (any(contribution > 0.0))
                {
                    bdptSplat(pixel, contribution);
                }
            }
            else
            {
                radiance += contribution;
            }
        }
    }

    return radiance;
}

#endif
