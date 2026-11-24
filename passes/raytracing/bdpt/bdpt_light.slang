struct BdptLightDistributionEntry
{
    uint alias;
    float acceptance;
    float probability;
    uint padding;
};

StructuredBuffer<BdptLightDistributionEntry> lightDistribution;

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
    LightRanges ranges = sceneGlobals[0].lightRanges;
    return ranges.pointEnd + ranges.spotCount + triangleLightCount() + uint(HAS_ENV);
}

BdptLightReference bdptLightAt(uint index)
{
    LightRanges ranges = sceneGlobals[0].lightRanges;
    if (index < ranges.pointCount)
    {
        return {BdptLightType::Point, index};
    }
    index -= ranges.pointCount;
    if (index < ranges.spotCount)
    {
        return {BdptLightType::Spot, index};
    }
    index -= ranges.spotCount;
    if (index < ranges.directionalEnd)
    {
        return {BdptLightType::Directional, index};
    }
    index -= ranges.directionalEnd;
    if (index < triangleLightCount())
    {
        return {BdptLightType::Area, index};
    }

    return {BdptLightType::Environment, 0};
}

uint bdptLightIndex(BdptLightReference light)
{
    LightRanges ranges = sceneGlobals[0].lightRanges;
    if (light.type == BdptLightType::Point)
    {
        return light.index;
    }
    if (light.type == BdptLightType::Spot)
    {
        return ranges.pointCount + light.index;
    }
    if (light.type == BdptLightType::Directional)
    {
        return ranges.pointCount + ranges.spotCount + light.index;
    }
    if (light.type == BdptLightType::Area)
    {
        return ranges.pointEnd + ranges.spotCount + light.index;
    }

    return bdptLightCount() - 1;
}

float bdptLightPmf(BdptLightReference light)
{
    return light.type == BdptLightType::None ? 0.0 : lightDistribution[bdptLightIndex(light)].probability;
}

BdptLightReference bdptSelectLight(inout uint state)
{
    uint count = bdptLightCount();
    if (count == 0)
    {
        return {BdptLightType::None, 0};
    }

    // Vose alias table: constant-time sampling and an explicit normalized PMF for MIS.
    float sampleValue = randomFloat(state) * float(count);
    uint index = min(uint(sampleValue), count - 1);
    BdptLightDistributionEntry entry = lightDistribution[index];
    uint selected = sampleValue - float(index) < entry.acceptance ? index : entry.alias;
    if (lightDistribution[selected].probability == 0.0)
    {
        return {BdptLightType::None, 0};
    }

    return bdptLightAt(selected);
}

bool bdptDeltaLight(BdptLightReference light)
{
    return light.type == BdptLightType::Point || light.type == BdptLightType::Spot ||
        light.type == BdptLightType::Directional;
}

LightData bdptPunctualLight(BdptLightReference light)
{
    LightRanges ranges = sceneGlobals[0].lightRanges;
    return lights[(light.type == BdptLightType::Point ? ranges.directionalEnd : ranges.rectAreaEnd) + light.index];
}

float bdptEmissionDirectionPdf(BdptVertex vertex, float3 direction)
{
    if (vertex.light.type == BdptLightType::Area)
    {
        LightTriangle triangle = loadTriangleLight(vertex.light.index);
        float cosine = dot(triangle.geometricNormal, direction);
        if (triangle.twoSided != 0)
        {
            return abs(cosine) * (0.5 * INV_PI);
        }

        return max(0.0, cosine) * INV_PI;
    }
    if (vertex.light.type == BdptLightType::Point)
    {
        return 1.0 / (4.0 * PI);
    }
    if (vertex.light.type == BdptLightType::Spot)
    {
        LightData light = bdptPunctualLight(vertex.light);

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
    if (vertex.light.type == BdptLightType::Area)
    {
        float area = loadTriangleLight(vertex.light.index).area;

        return area > 0.0 ? probability / area : 0.0;
    }

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
             !withinLightRange(bdptPunctualLight(light.light), distanceSquared)))
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
    if (sampled.light.type == BdptLightType::Point || sampled.light.type == BdptLightType::Spot)
    {
        LightData light = bdptPunctualLight(sampled.light);
        sampled.position = light.position;
        float3 offset = sampled.position - reference.position;
        float distanceSquared = dot(offset, offset);
        if (distanceSquared == 0.0 || !withinLightRange(light, distanceSquared))
        {
            return false;
        }
        direction = offset * rsqrt(distanceSquared);
        float attenuation = light.type == LightType::Spot ? spotAngularAttenuation(light, -direction) : 1.0;
        radiance = light.color * (attenuation / distanceSquared);
    }
    else if (sampled.light.type == BdptLightType::Area)
    {
        sampled.position = sampleTriangle(sampled.light.index, bdptRandom2D(state), sampled.uv);
        LightTriangle triangle = loadTriangleLight(sampled.light.index);
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
        radiance = areaEmission(sampled.light.index, sampled.uv, -direction);
    }
    else
    {
        if (sampled.light.type == BdptLightType::Directional)
        {
            LightData light = lights[sampled.light.index];
            direction = -light.direction.xyz;
            radiance = light.color;
        }
        else
        {
            direction = bdptUniformSphere(bdptRandom2D(state));
            pdf = 1.0 / (4.0 * PI);
            radiance = environmentRadiance(direction);
        }

        sampled.wo = direction;
        sampled.position = reference.position + direction * (2.0 * parameters.sceneRadius);
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
    if (endpoint.light.type == BdptLightType::Area)
    {
        LightTriangle triangle = loadTriangleLight(endpoint.light.index);
        if (triangle.area == 0.0)
        {
            return false;
        }
        endpoint.position = sampleTriangle(endpoint.light.index, positionSample, endpoint.uv);
        endpoint.normal = endpoint.geometricNormal = triangle.geometricNormal;

        float side = 1.0;
        if (triangle.twoSided != 0)
        {
            side = directionSample.x < 0.5 ? 1.0 : -1.0;
            directionSample.x = frac(directionSample.x * 2.0);
        }

        float radius = sqrt(directionSample.x);
        float phi = 2.0 * PI * directionSample.y;
        float3 local = float3(radius * cos(phi), radius * sin(phi), sqrt(1.0 - directionSample.x));
        ray.Direction = bdptFrameDirection(triangle.geometricNormal * side, local);
        cosine = local.z;
        positionPdf = 1.0 / triangle.area;
        directionPdf = cosine * INV_PI * (triangle.twoSided != 0 ? 0.5 : 1.0);
        radiance = areaEmission(endpoint.light.index, endpoint.uv, ray.Direction);
    }
    else if (endpoint.light.type == BdptLightType::Point)
    {
        LightData light = bdptPunctualLight(endpoint.light);
        endpoint.position = light.position;
        ray.Direction = bdptUniformSphere(directionSample);
        directionPdf = 1.0 / (4.0 * PI);
        radiance = light.color;
    }
    else if (endpoint.light.type == BdptLightType::Spot)
    {
        LightData light = bdptPunctualLight(endpoint.light);
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
            LightData light = lights[endpoint.light.index];
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
