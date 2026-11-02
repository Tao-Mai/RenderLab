enum class BdptVertexType
{
    Camera,
    Surface,
    Light,
};

enum class BdptLightType
{
    None,
    Point,
    Spot,
    Directional,
    Area,
    Environment,
};

struct BdptLightReference
{
    BdptLightType type;
    uint index;
};

// Incident throughput and densities in the path-space area measure.
// Infinite endpoints use directional measure instead.
struct BdptVertex
{
    float3 position;
    float3 normal;
    float3 geometricNormal;
    float3 tangent;
    float2 uv;

    float3 wo;
    float3 beta;

    float pdfFwd;
    float pdfRev;

    OfflineMaterialPreviewModel model;
    float3 albedo;
    ConductorBrdf conductor;
    float eta;

    uint delta;
    BdptVertexType type;
    BdptLightReference light;

    bool isInfinite()
    {
        return type == BdptVertexType::Light &&
            (light.type == BdptLightType::Directional || light.type == BdptLightType::Environment);
    }

    bool isOnSurface()
    {
        return type == BdptVertexType::Surface || light.type == BdptLightType::Area;
    }

    bool isLight()
    {
        return type == BdptVertexType::Light || light.type == BdptLightType::Area;
    }
};

float3 bdptDirection(BdptVertex from, BdptVertex to)
{
    if (to.isInfinite())
    {
        return to.wo;
    }
    if (from.isInfinite())
    {
        return -from.wo;
    }

    return normalize(to.position - from.position);
}

float bdptConvertDensity(BdptVertex from, float pdf, BdptVertex to)
{
    if (to.isInfinite())
    {
        return pdf;
    }

    float3 offset = to.position - from.position;
    float distanceSquared = dot(offset, offset);
    if (distanceSquared == 0.0)
    {
        return 0.0;
    }
    if (to.isOnSurface())
    {
        pdf *= abs(dot(to.geometricNormal, offset * rsqrt(distanceSquared)));
    }

    return pdf / distanceSquared;
}

struct BdptBsdf
{
    float3 normal;
    float3 geometricNormal;
    float3 tangent;
    float3 bitangent;

    float3 wo;

    DiffuseBrdf diffuse;
    ConductorBrdf conductor;
    SmoothDielectricBsdf dielectric;

    OfflineMaterialPreviewModel model;

    bool connectible()
    {
        return model != OfflineMaterialPreviewModel::SmoothDielectric &&
            (model != OfflineMaterialPreviewModel::Conductor || !conductor.isDelta());
    }

    float3 toLocal(float3 direction)
    {
        return float3(dot(tangent, direction), dot(bitangent, direction), dot(normal, direction));
    }

    float shadingNormalCorrection(float3 wi, TransportMode mode)
    {
        if (mode == TransportMode::Radiance)
        {
            return 1.0;
        }

        float denominator = abs(dot(wo, geometricNormal) * dot(wi, normal));
        if (denominator == 0.0)
        {
            return 0.0;
        }

        return abs(dot(wo, normal) * dot(wi, geometricNormal)) / denominator;
    }

    float3 evaluate(float3 wi, TransportMode mode)
    {
        if (!connectible() || dot(wo, normal) <= 0.0 || dot(wi, normal) <= 0.0 ||
            dot(wo, geometricNormal) * dot(wi, geometricNormal) <= 0.0)
        {
            return float3(0.0);
        }

        float3 f = model == OfflineMaterialPreviewModel::Conductor
            ? conductor.evaluate(toLocal(wo), toLocal(wi)) : diffuse.evaluate();

        return f * shadingNormalCorrection(wi, mode);
    }

    float pdf(float3 wi)
    {
        if (!connectible() || dot(wo, normal) <= 0.0 ||
            dot(wo, geometricNormal) * dot(wi, geometricNormal) <= 0.0)
        {
            return 0.0;
        }

        return model == OfflineMaterialPreviewModel::Conductor
            ? conductor.pdf(toLocal(wo), toLocal(wi)) : diffuse.pdf(normal, wi);
    }

    BrdfSample sample(float branchSample, float2 directionSample, TransportMode mode)
    {
        BrdfSample result = {};
        if (dot(wo, normal) <= 0.0)
        {
            return result;
        }
        if (model == OfflineMaterialPreviewModel::SmoothDielectric)
        {
            result = dielectric.sample(wo, normal, branchSample, mode);
            // Reject shading-normal events on the wrong geometric side.
            bool shadingReflection = dot(wo, normal) * dot(result.direction, normal) > 0.0;
            bool geometricReflection = dot(wo, geometricNormal) * dot(result.direction, geometricNormal) > 0.0;
            if (shadingReflection != geometricReflection)
            {
                result.weight = float3(0.0);
            }
        }
        else if (model == OfflineMaterialPreviewModel::Conductor)
        {
            result = conductor.sample(toLocal(wo), directionSample);
            result.direction = tangent * result.direction.x + bitangent * result.direction.y
                + normal * result.direction.z;
        }
        else
        {
            result = diffuse.sample(normal, directionSample);
        }
        if (model != OfflineMaterialPreviewModel::SmoothDielectric &&
            dot(wo, geometricNormal) * dot(result.direction, geometricNormal) <= 0.0)
        {
            result.weight = float3(0.0);
        }

        result.weight *= shadingNormalCorrection(result.direction, mode);

        return result;
    }
};

void bdptPrepareMaterial(inout BdptVertex vertex, uint materialIndex, uint primitiveIndex)
{

    MaterialPreview header = materialInstructions[materialIndex];
    vertex.model = header.model;
    if (header.model == OfflineMaterialPreviewModel::SmoothDielectric)
    {
        float3 eta = evaluateTexture(header.firstTexture, vertex.uv, vertex.position, vertex.normal, primitiveIndex);
        vertex.eta = max((eta.x + eta.y + eta.z) / 3.0, 1e-4);
    }
    else if (header.model == OfflineMaterialPreviewModel::Conductor)
    {
        vertex.conductor = evaluateConductor(header, vertex.uv, vertex.position, vertex.normal, primitiveIndex);
    }
    else
    {
        vertex.albedo = saturate(evaluateMaterial(materialIndex, vertex.uv, vertex.position, vertex.normal, primitiveIndex));
    }
}

BdptBsdf bdptBsdf(BdptVertex vertex, float3 wo)
{
    BdptBsdf result = {};
    result.model = vertex.model;
    result.wo = wo;
    result.geometricNormal = dot(vertex.geometricNormal, wo) >= 0.0
        ? vertex.geometricNormal : -vertex.geometricNormal;
    result.normal = dot(vertex.normal, result.geometricNormal) >= 0.0 ? vertex.normal : -vertex.normal;
    float3 tangent = vertex.tangent - result.normal * dot(result.normal, vertex.tangent);
    if (dot(tangent, tangent) == 0.0)
    {
        float3 axis = abs(result.normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
        tangent = cross(axis, result.normal);
    }
    result.tangent = normalize(tangent);
    result.bitangent = cross(result.normal, result.tangent);

    if (vertex.model == OfflineMaterialPreviewModel::SmoothDielectric)
    {
        result.dielectric.eta = dot(vertex.geometricNormal, wo) >= 0.0 ? vertex.eta : 1.0 / vertex.eta;
    }
    result.conductor = vertex.conductor;
    result.diffuse.albedo = vertex.albedo;

    return result;
}

float3 bdptOffset(BdptVertex vertex, float3 direction)
{
    float side = dot(vertex.geometricNormal, direction) >= 0.0 ? 1.0 : -1.0;

    return vertex.position + vertex.geometricNormal * (side * kRayEpsilon);
}
