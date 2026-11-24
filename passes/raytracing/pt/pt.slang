#ifndef RENDERLAB_PT_SLANG
#define RENDERLAB_PT_SLANG

#include "raytracing/path_common.slang"
#include "light_sampling.slang"

float balanceHeuristic(float pdf, float otherPdf)
{
    return pdf / (pdf + otherPdf);
}

struct Surface
{
    float3 origin;
    float3 geometricNormal;
    SurfaceBsdf scattering;

    float3 evaluateLight(float3 direction, float3 incident)
    {
        float cosine = dot(scattering.normal, direction);

        return scattering.evaluate(direction) * cosine * incident;
    }

    float3 deltaRadiance(DeltaLightSample selected)
    {
        if (selected.probability == 0.0 || dot(scattering.normal, selected.direction) <= 0.0 ||
            dot(geometricNormal, selected.direction) <= 0.0)
        {
            return float3(0.0);
        }
        if (!visible(origin, selected.direction, selected.distance))
        {
            return float3(0.0);
        }

        return evaluateLight(selected.direction, selected.incidentIrradiance) / selected.probability;
    }

    float3 areaRadiance(AreaLightSample selected, float2 sampleValue)
    {
        if (selected.probability == 0.0)
        {
            return float3(0.0);
        }

        float2 uv;
        float3 target = sampleTriangle(selected.index, sampleValue, uv);
        float3 offset = target - origin;
        float distance = length(offset);
        if (distance <= kRayEpsilon)
        {
            return float3(0.0);
        }

        float3 direction = offset / distance;
        float lightPdf = selected.probability * areaPdf(selected.index, origin, target);
        if (lightPdf == 0.0 || dot(scattering.normal, direction) <= 0.0 || dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        if (!visible(origin, direction, distance))
        {
            return float3(0.0);
        }

        float weight = balanceHeuristic(lightPdf, scattering.pdf(direction));
        float3 emission = areaEmission(selected.index, uv, -direction);

        return evaluateLight(direction, emission) * (weight / lightPdf);
    }
};

Surface makeSurface(Hit hit, ScatteringMaterial material, float3 wo)
{
    Surface surface;
    bool frontFace = dot(hit.geometricNormal, wo) >= 0.0;
    surface.geometricNormal = frontFace ? hit.geometricNormal : -hit.geometricNormal;
    surface.origin = hit.position + surface.geometricNormal * kRayEpsilon;

    // Orient reflective shading normals to the hit side. Transmission starts with the original orientation.
    bool canTransmit = material.canTransmit();
    float3 normalReference = canTransmit ? hit.geometricNormal : surface.geometricNormal;
    float3 normal = dot(hit.normal, normalReference) >= 0.0 ? hit.normal : -hit.normal;
    if (dot(normal, wo) <= 0.0)
    {
        normal = surface.geometricNormal;
    }
    else if (canTransmit && dot(normal, surface.geometricNormal) < 0.0)
    {
        normal = -normal;
    }

    surface.scattering = {material, normal, hit.tangent, wo, frontFace};

    return surface;
}

float3 tracePath(RayDesc ray, inout uint state)
{
    float3 radiance = float3(0.0);
    float3 throughput = float3(1.0);
    float previousBrdfPdf = 0.0;
    float previousAreaWeightSum = 0.0;
    uint previousDelta = 0;

    for (uint bounce = 0; bounce <= parameters.path.maxBounces; ++bounce)
    {
        Hit hit = {};
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
            kSceneMask | kLightMask, 0, 0, 0, ray, hit);

        // 未命中，直接返回环境光
        if (hit.hit == 0)
        {
            radiance += throughput * environmentRadiance(ray.Direction);
            break;
        }

        // 命中发光几何体，如果直接命中那权重为1，如果间接命中则根据上个表面对该面的选择概率来计算权重
        // 发光几何体本身也可以有材质，所以继续往下追踪
        if (hit.areaLightIndex != kInvalidLightIndex)
        {
            uint lightIndex = hit.areaLightIndex;
            float weight = 1.0;
            if (bounce != 0 && previousDelta == 0)
            {
                float probability = previousAreaWeightSum > 0.0
                    ? areaSelectionWeight(lightIndex, ray.Origin) / previousAreaWeightSum : 0.0;
                float lightPdf = probability * areaPdf(lightIndex, ray.Origin, hit.position);
                weight = balanceHeuristic(previousBrdfPdf, lightPdf);
            }
            radiance += throughput * areaEmission(lightIndex, hit.uv, -ray.Direction) * weight;
        }

        if (hit.emitterOnly != 0 || bounce == parameters.path.maxBounces)
        {
            break;
        }

        ScatteringMaterial material = evaluateScattering(
            materials[hit.materialIndex], hit.uv, hit.position, hit.normal, hit.primitiveIndex);
        if (!material.hasScattering())
        {
            break;
        }
        Surface surface = makeSurface(hit, material, -ray.Direction);
        float areaWeightSum = 0.0;

        // Delta events have no continuous density for light sampling.
        if (!material.isDelta())
        {
            LightRanges ranges = sceneGlobals[0].lightRanges;
            DeltaLightSample local = sampleLocalLight(ranges, surface.origin, state);
            DeltaLightSample distant = sampleDistantLight(ranges.directionalEnd, state);
            AreaLightSample area = sampleAreaLight(triangleLightCount(), surface.origin, state);
            float2 areaSample;
            areaSample.x = randomFloat(state);
            areaSample.y = randomFloat(state);
            radiance += throughput * (surface.deltaRadiance(local) + surface.deltaRadiance(distant) +
                surface.areaRadiance(area, areaSample));
            areaWeightSum = area.weightSum;
        }

        pbrt::BsdfSample sampled = surface.scattering.sample(state);
        float side = dot(surface.geometricNormal, sampled.direction);
        if ((sampled.delta == 0 && sampled.pdf <= 0.0) || (!material.canTransmit() && side <= 0.0) ||
            !any(sampled.weight > 0.0))
        {
            break;
        }

        throughput *= sampled.weight;
        previousBrdfPdf = sampled.pdf;
        previousDelta = sampled.delta;
        previousAreaWeightSum = areaWeightSum;
        ray.Origin = hit.position + surface.geometricNormal * (side < 0.0 ? -kRayEpsilon : kRayEpsilon);
        ray.Direction = sampled.direction;
        ray.TMin = 0.0;
        ray.TMax = kMaxRayDistance;
    }

    return radiance;
}

#endif
