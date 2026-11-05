#ifndef RENDERLAB_RESTIR_SLANG
#define RENDERLAB_RESTIR_SLANG

#include "raytracing/realtime/restir_data.slang"

RestirSurfaceData restirPackSurface(Surface surface, float depth)
{
    RestirSurfaceData result = {};
    result.position = surface.position;
    result.depth = depth;
    result.normal = surface.normal;
    result.geometricNormal = surface.geometricNormal;
    result.viewDirection = surface.viewDirection;
    result.brdf = surface.brdf;
    result.materialIndex = surface.materialIndex;
    result.objectIndex = surface.objectIndex;
    return result;
}

Surface restirUnpackSurface(RestirSurfaceData data)
{
    Surface result = {};
    result.position = data.position;
    result.normal = data.normal;
    result.geometricNormal = data.geometricNormal;
    result.viewDirection = data.viewDirection;
    result.brdf = data.brdf;
    result.materialIndex = data.materialIndex;
    result.objectIndex = data.objectIndex;
    result.emissiveTriangleIndex = kInvalidEmissiveTriangle;
    return result;
}

uint restirUniformIndex(uint count, inout uint state)
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

uint restirGroupCount()
{
    return uint(parameters.lightRanges.spotEnd > 0) + uint(emissiveTriangles.Load(0) > 0)
        + uint(parameters.iblEnabled != 0);
}

float restirSelectionPdf(RestirLightSample sample)
{
    uint count = sample.kind == kRestirTriangleLight ? emissiveTriangles.Load(0)
        : sample.kind == kRestirAnalyticLight ? parameters.lightRanges.spotEnd : 1;
    return 1.0 / (float(restirGroupCount()) * float(count));
}

RestirLightSample restirSampleLight(inout uint state)
{
    RestirLightSample result = {};
    uint groups = restirGroupCount();
    if (groups == 0)
    {
        return result;
    }

    uint group = restirUniformIndex(groups, state);
    uint analyticCount = parameters.lightRanges.spotEnd;
    uint triangleCount = emissiveTriangles.Load(0);
    if (analyticCount > 0)
    {
        if (group == 0)
        {
            result.kind = kRestirAnalyticLight;
            result.index = restirUniformIndex(analyticCount, state);
        }
        else
        {
            --group;
        }
    }
    if (result.kind == kRestirInvalidLight && triangleCount > 0)
    {
        if (group == 0)
        {
            result.kind = kRestirTriangleLight;
            result.index = restirUniformIndex(triangleCount, state);
        }
        else
        {
            --group;
        }
    }
    if (result.kind == kRestirInvalidLight)
    {
        result.kind = kRestirEnvironmentLight;
    }

    result.uv.x = randomFloat(state);
    result.uv.y = randomFloat(state);
    return result;
}

struct RestirLightEvaluation
{
    float3 contribution; // Integrand over discrete light ID and uniform unit-square sample coordinates.
    float3 direction;
    float distance;
    float conditionalPdf; // Conditional solid-angle density; 1 for delta lights.
    uint delta;

    property float target { get { return dot(contribution, kRestirLuminanceWeights); } }
};

RestirLightEvaluation restirEvaluate(Surface surface, RestirLightSample sample)
{
    RestirLightEvaluation result = {};
    float3 origin = surface.rayOrigin();
    float3 emission = float3(0.0);
    float3 lightPosition = float3(0.0);
    float3 lightNormal = float3(0.0);
    float area = 0.0;
    bool twoSided = false;

    if (sample.kind == kRestirTriangleLight)
    {
        EmissiveTriangle triangle = loadEmissiveTriangle(sample.index);
        if (triangle.area <= 0.0)
        {
            return result;
        }

        float3 barycentrics = triangleBarycentrics(sample.uv);
        lightPosition = triangle.position + triangle.edge1 * barycentrics.y + triangle.edge2 * barycentrics.z;
        lightNormal = cross(triangle.edge1, triangle.edge2) / (2.0 * triangle.area);
        area = triangle.area;
        twoSided = materials[triangle.materialIndex].doubleSided != 0;
        emission = triangleLightEmission(triangle, barycentrics);
    }
    else if (sample.kind == kRestirAnalyticLight)
    {
        LightData light = lights[sample.index];
        emission = light.color;
        if (light.type == LightType::Directional)
        {
            result.direction = -normalize(light.direction.xyz);
            result.distance = kMaxRayDistance;
            result.conditionalPdf = 1.0;
            result.delta = 1;
            result.contribution = surface.evaluateLight(result.direction, emission);
            return result;
        }

        if (light.type == LightType::RectArea)
        {
            float3 up = normalize(cross(light.direction.xyz, light.areaRight));
            lightPosition = light.position + light.areaRight * ((sample.uv.x - 0.5) * light.areaSize.x)
                + up * ((sample.uv.y - 0.5) * light.areaSize.y);
            lightNormal = normalize(light.direction.xyz);
            area = light.areaSize.x * light.areaSize.y;
        }
        else
        {
            float3 offset = light.position - origin;
            float distanceSquared = dot(offset, offset);
            if (distanceSquared == 0.0 || distanceSquared > light.range * light.range)
            {
                return result;
            }

            result.distance = sqrt(distanceSquared);
            result.direction = offset / result.distance;
            float attenuation = light.type == LightType::Spot ? spotAngularAttenuation(light, -result.direction) : 1.0;
            result.conditionalPdf = 1.0;
            result.delta = 1;
            result.contribution = surface.evaluateLight(result.direction, emission * (attenuation / distanceSquared));
            return result;
        }
    }
    else if (sample.kind == kRestirEnvironmentLight)
    {
        float elevation = 1.0 - 2.0 * sample.uv.x;
        float radius = sqrt(max(0.0, 1.0 - elevation * elevation));
        float angle = 2.0 * PI * sample.uv.y;
        result.direction = float3(radius * cos(angle), elevation, radius * sin(angle));
        result.distance = kMaxRayDistance;
        result.conditionalPdf = 1.0 / (4.0 * PI);
        emission = radianceMap.SampleLevel(radianceSampler, result.direction, 0.0).rgb;
        result.contribution = surface.evaluateLight(result.direction, emission) / result.conditionalPdf;
        return result;
    }
    else
    {
        return result;
    }

    float3 offset = lightPosition - origin;
    float distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0 || area <= 0.0)
    {
        return result;
    }

    result.distance = sqrt(distanceSquared);
    result.direction = offset / result.distance;
    float cosine = dot(lightNormal, -result.direction);
    cosine = twoSided ? abs(cosine) : max(0.0, cosine);
    if (cosine <= 0.0)
    {
        return result;
    }

    result.conditionalPdf = distanceSquared / (area * cosine);
    result.contribution = surface.evaluateLight(result.direction, emission) / result.conditionalPdf;
    return result;
}

float restirMixturePdf(Surface surface, RestirLightSample sample, RestirLightEvaluation evaluated)
{
    float brdfPdf = 0.0;
    if (evaluated.delta == 0 && evaluated.conditionalPdf > 0.0
        && dot(surface.geometricNormal, evaluated.direction) > 0.0)
    {
        brdfPdf = surface.brdf.pdf(surface.normal, surface.viewDirection, evaluated.direction) / evaluated.conditionalPdf;
    }

    float lightCount = float(parameters.candidateCount);
    return (lightCount * restirSelectionPdf(sample) + brdfPdf) / (lightCount + 1.0);
}

RestirLightSample restirSampleBrdf(Surface surface, inout uint state)
{
    RestirLightSample result = {};
    float3 randomSample;
    randomSample.x = randomFloat(state);
    randomSample.y = randomFloat(state);
    randomSample.z = randomFloat(state);
    BrdfSample sampled = surface.brdf.sample(surface.normal, surface.viewDirection, randomSample);
    if (sampled.pdf <= 0.0 || dot(surface.geometricNormal, sampled.direction) <= 0.0 || !any(sampled.weight > 0.0))
    {
        return result;
    }

    RayDesc ray;
    ray.Origin = surface.rayOrigin();
    ray.Direction = sampled.direction;
    ray.TMin = kRayEpsilon;
    ray.TMax = kMaxRayDistance;
    PathHit hit = {};
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        kSceneMask | kLightMask, 0, 0, 0, ray, hit);
    if (hit.event == PathEvent::Surface && hit.surface.emissiveTriangleIndex != kInvalidEmissiveTriangle)
    {
        result.kind = kRestirTriangleLight;
        result.index = hit.surface.emissiveTriangleIndex;
        result.uv = triangleSampleUv(hit.barycentrics);
    }
    else if (hit.event == PathEvent::Emitter)
    {
        result.kind = kRestirAnalyticLight;
        result.index = hit.lightIndex;
        LightData light = lights[hit.lightIndex];
        float3 up = normalize(cross(light.direction.xyz, light.areaRight));
        float3 offset = ray.Origin + ray.Direction * hit.distance - light.position;
        result.uv = float2(dot(offset, light.areaRight), dot(offset, up)) / light.areaSize + 0.5;
    }
    else if (hit.event == PathEvent::Miss && parameters.iblEnabled != 0)
    {
        result.kind = kRestirEnvironmentLight;
        result.uv = float2((1.0 - sampled.direction.y) * 0.5,
            frac(atan2(sampled.direction.z, sampled.direction.x) / (2.0 * PI)));
    }
    return result;
}

void restirDiscardOccluded(Surface surface, inout RestirReservoir reservoir)
{
    if (reservoir.valid)
    {
        RestirLightEvaluation evaluated = restirEvaluate(surface, reservoir.sample);
        if (evaluated.target <= 0.0 || !surface.isVisible(evaluated.direction, evaluated.distance))
        {
            reservoir.weightSum = 0.0;
        }
    }
}

RestirReservoir restirGenerateInitial(Surface surface, inout uint state)
{
    RestirReservoir reservoir = {};
    for (uint candidate = 0; candidate <= parameters.candidateCount; ++candidate)
    {
        RestirLightSample sample = candidate == parameters.candidateCount
            ? restirSampleBrdf(surface, state) : restirSampleLight(state);
        RestirLightEvaluation evaluated = restirEvaluate(surface, sample);
        float sourcePdf = sample.kind != kRestirInvalidLight ? restirMixturePdf(surface, sample, evaluated) : 0.0;
        float weight = sourcePdf > 0.0 ? evaluated.target / sourcePdf : 0.0;
        reservoir.stream(sample, evaluated.target, weight, 1, state);
    }

    reservoir.finalize(float(reservoir.sampleCount));

    // Original paper, Algorithm 5: discard an occluded initial sample but retain M.
    restirDiscardOccluded(surface, reservoir);
    return reservoir;
}

float3 restirShade(Surface surface, inout RestirReservoir reservoir)
{
    if (!reservoir.valid)
    {
        return float3(0.0);
    }

    RestirLightEvaluation evaluated = restirEvaluate(surface, reservoir.sample);
    if (evaluated.target <= 0.0 || !surface.isVisible(evaluated.direction, evaluated.distance))
    {
        reservoir.weightSum = 0.0;
        return float3(0.0);
    }

    return evaluated.contribution * reservoir.weightSum;
}

#endif
