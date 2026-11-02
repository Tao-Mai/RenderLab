#ifndef RENDERLAB_OFFLINE_PT_SLANG
#define RENDERLAB_OFFLINE_PT_SLANG

#include "raytracing/offline/path_common.slang"

float balanceHeuristic(float pdf, float otherPdf)
{
    return pdf / (pdf + otherPdf);
}

struct Surface
{
    float3 origin;
    float3 normal;
    float3 geometricNormal;
    float3 viewDirection;
    float3 tangent;
    float3 bitangent;
    DiffuseBrdf brdf;
    ConductorBrdf conductor;
    bool isConductor;

    float3 toLocal(float3 direction)
    {
        return float3(dot(tangent, direction), dot(bitangent, direction), dot(normal, direction));
    }

    float brdfPdf(float3 direction)
    {
        if (isConductor)
        {
            return conductor.pdf(toLocal(viewDirection), toLocal(direction));
        }

        return brdf.pdf(normal, direction);
    }

    BrdfSample sampleBrdf(float2 sampleValue)
    {
        if (isConductor)
        {
            BrdfSample result = conductor.sample(toLocal(viewDirection), sampleValue);
            result.direction = tangent * result.direction.x + bitangent * result.direction.y
                + normal * result.direction.z;

            return result;
        }

        return brdf.sample(normal, sampleValue);
    }

    float3 evaluate(float3 direction, float3 incident)
    {
        if (dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }

        float3 value = isConductor ? conductor.evaluate(toLocal(viewDirection), toLocal(direction)) : brdf.evaluate();

        return value * max(0.0, dot(normal, direction)) * incident;
    }

    float3 deltaRadiance(DeltaLightSample selected)
    {
        if (selected.probability == 0.0 || dot(normal, selected.direction) <= 0.0 ||
            dot(geometricNormal, selected.direction) <= 0.0)
        {
            return float3(0.0);
        }
        if (!visible(origin, selected.direction, selected.distance))
        {
            return float3(0.0);
        }

        return evaluate(selected.direction, selected.incidentIrradiance) / selected.probability;
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
        if (lightPdf == 0.0 || dot(normal, direction) <= 0.0 || dot(geometricNormal, direction) <= 0.0)
        {
            return float3(0.0);
        }
        if (!visible(origin, direction, distance))
        {
            return float3(0.0);
        }

        float weight = balanceHeuristic(lightPdf, brdfPdf(direction));
        float3 emission = areaEmission(selected.index, uv, -direction);

        return evaluate(direction, emission) * (weight / lightPdf);
    }
};

float3 tracePath(RayDesc ray, inout uint state)
{
    float3 radiance = float3(0.0);
    float3 throughput = float3(1.0);
    float previousBrdfPdf = 0.0;
    float previousAreaWeightSum = 0.0;
    uint previousDelta = 0;

    for (uint bounce = 0; bounce <= parameters.maxBounces; ++bounce)
    {
        Hit hit = {};
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, kSceneMask, 0, 0, 0, ray, hit);

        // 未命中，直接返回环境光
        if (hit.hit == 0)
        {
            radiance += throughput * environmentEmission(parameters.hasEnvironmentLight, ray.Direction);
            break;
        }

        HitData object = objects[hit.objectIndex];

        // 命中发光几何体，如果直接命中那权重为1，如果间接命中则根据上个表面对该面的选择概率来计算权重
        // 发光几何体本身也可以有材质，所以继续往下追踪
        if (object.firstAreaLight != kOfflineInvalidIndex)
        {
            uint lightIndex = object.firstAreaLight + hit.primitiveIndex;
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

        if (bounce == parameters.maxBounces)
        {
            break;
        }

        float3 wo = -ray.Direction;
        MaterialPreview header = materialInstructions[object.materialIndex];
        BrdfSample sampled;
        sampled.direction = float3(0.0);
        sampled.weight = float3(0.0);
        sampled.pdf = 0.0;
        sampled.delta = 0;
        float3 nextOrigin = hit.position;
        float areaWeightSum = 0.0;

        if (header.model == OfflineMaterialPreviewModel::SmoothDielectric)
        {
            float3 geometricNormal = hit.geometricNormal;
            float3 shadingNormal = hit.normal;
            if (dot(shadingNormal, geometricNormal) < 0.0)
            {
                shadingNormal = -shadingNormal;
            }

            float3 facingNormal = dot(geometricNormal, wo) >= 0.0 ? geometricNormal : -geometricNormal;
            if (dot(shadingNormal, wo) <= 0.0)
            {
                shadingNormal = facingNormal;
            }
            else if (dot(shadingNormal, facingNormal) < 0.0)
            {
                shadingNormal = -shadingNormal;
            }

            float3 etaRgb = evaluateTexture(header.firstTexture, hit.uv, hit.position, hit.normal, hit.primitiveIndex);
            float materialEta = max((etaRgb.x + etaRgb.y + etaRgb.z) / 3.0, 1e-4);
            float relativeEta = dot(geometricNormal, wo) >= 0.0 ? materialEta : 1.0 / materialEta;

            SmoothDielectricBsdf bsdf;
            bsdf.eta = relativeEta;
            sampled = bsdf.sample(wo, shadingNormal, randomFloat(state), TransportMode::Radiance);
            if (!any(sampled.direction != 0.0))
            {
                break;
            }

            bool transmitted = dot(sampled.direction, facingNormal) < 0.0;
            nextOrigin = hit.position + facingNormal * (transmitted ? -kRayEpsilon : kRayEpsilon);
        }
        else
        {
            Surface surface = {};
            surface.viewDirection = wo;

            // 光追没有背面剔除，只要命中了就要给个颜色，所以需要自适应调整法线方向
            // 先将着色法线调到几何法线一致的半球，再考虑和光线的交互
            surface.geometricNormal = dot(hit.geometricNormal, wo) >= 0.0 ? hit.geometricNormal : -hit.geometricNormal;
            surface.normal = dot(hit.normal, surface.geometricNormal) >= 0.0 ? hit.normal : -hit.normal;

            // 法线插值后的平滑法线和真实几何不一致，所以着色法线和光线大于90度时仍然会命中，但不能直接变黑，所以用三角形法线替代
            if (dot(surface.normal, wo) <= 0.0)
            {
                surface.normal = surface.geometricNormal;
            }

            // 用几何法线向外偏移一点，避免自相交
            surface.origin = hit.position + surface.geometricNormal * kRayEpsilon;
            surface.isConductor = header.model == OfflineMaterialPreviewModel::Conductor;
            if (surface.isConductor)
            {
                surface.conductor = evaluateConductor(header, hit.uv, hit.position, hit.normal, hit.primitiveIndex);
                surface.tangent = normalize(hit.tangent - surface.normal * dot(surface.normal, hit.tangent));
                surface.bitangent = cross(surface.normal, surface.tangent);
            }
            else
            {
                surface.brdf.albedo = saturate(evaluateMaterial(object.materialIndex, hit.uv, hit.position, hit.normal,
                    hit.primitiveIndex));
                if (!any(surface.brdf.albedo > 0.0))
                {
                    break;
                }
            }

            // 采样光源，面光源用MIS
            if (!surface.isConductor || !surface.conductor.isDelta())
            {
                DeltaLightSample local = sampleLocalLight(parameters.pointLightCount, parameters.spotLightCount,
                    surface.origin, state);
                DeltaLightSample distant = sampleDistantLight(parameters.directionalLightCount, state);
                AreaLightSample area = sampleAreaLight(parameters.triangleLightCount, surface.origin, state);
                float2 areaSample;
                areaSample.x = randomFloat(state);
                areaSample.y = randomFloat(state);
                radiance += throughput * (surface.deltaRadiance(local) + surface.deltaRadiance(distant) +
                    surface.areaRadiance(area, areaSample));
                areaWeightSum = area.weightSum;
            }

            float2 brdfSample;
            brdfSample.x = randomFloat(state);
            brdfSample.y = randomFloat(state);
            sampled = surface.sampleBrdf(brdfSample);
            if ((sampled.delta == 0 && sampled.pdf <= 0.0) || dot(surface.geometricNormal, sampled.direction) <= 0.0 ||
                !any(sampled.weight > 0.0))
            {
                break;
            }
            nextOrigin = surface.origin;
        }

        throughput *= sampled.weight;
        previousBrdfPdf = sampled.pdf;
        previousDelta = sampled.delta;
        previousAreaWeightSum = areaWeightSum;
        ray.Origin = nextOrigin;
        ray.Direction = sampled.direction;
        ray.TMin = 0.0;
        ray.TMax = kMaxRayDistance;
    }

    return radiance;
}

#endif
