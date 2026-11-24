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

    ScatteringMaterial material;

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
    SurfaceBsdf scattering;
    float3 geometricNormal;

    property normal : float3 {
        get { return scattering.normal; }
    }

    bool connectible() { return scattering.material.hasScattering() && !scattering.material.isDelta(); }

    float shadingNormalCorrection(float3 wi, pbrt::TransportMode mode)
    {
        if (mode == pbrt::TransportMode::Radiance)
        {
            return 1.0;
        }

        float denominator = abs(dot(scattering.wo, geometricNormal) * dot(wi, normal));
        if (denominator == 0.0)
        {
            return 0.0;
        }

        return abs(dot(scattering.wo, normal) * dot(wi, geometricNormal)) / denominator;
    }

    float3 evaluate(float3 wi, pbrt::TransportMode mode)
    {
        if (!connectible() || dot(scattering.wo, normal) <= 0.0 || dot(wi, normal) <= 0.0 ||
            dot(scattering.wo, geometricNormal) * dot(wi, geometricNormal) <= 0.0)
        {
            return float3(0.0);
        }

        return scattering.evaluate(wi) * shadingNormalCorrection(wi, mode);
    }

    float pdf(float3 wi)
    {
        if (!connectible() || dot(scattering.wo, normal) <= 0.0 ||
            dot(scattering.wo, geometricNormal) * dot(wi, geometricNormal) <= 0.0)
        {
            return 0.0;
        }

        return scattering.pdf(wi);
    }

    pbrt::BsdfSample sample(float branchSample, float2 directionSample, pbrt::TransportMode mode)
    {
        if (dot(scattering.wo, normal) <= 0.0)
        {
            return (pbrt::BsdfSample)0;
        }

        pbrt::BsdfSample result = scattering.sample(branchSample, directionSample, mode);
        bool geometricReflection = dot(scattering.wo, geometricNormal) * dot(result.direction, geometricNormal) > 0.0;
        if (scattering.material.canTransmit())
        {
            // Reject shading-normal events on the wrong geometric side.
            bool shadingReflection = dot(scattering.wo, normal) * dot(result.direction, normal) > 0.0;
            if (shadingReflection != geometricReflection)
            {
                result.weight = float3(0.0);
            }
        }
        else if (!geometricReflection)
        {
            result.weight = float3(0.0);
        }

        result.weight *= shadingNormalCorrection(result.direction, mode);

        return result;
    }
};

BdptBsdf bdptBsdf(BdptVertex vertex, float3 wo)
{
    bool frontFace = dot(vertex.geometricNormal, wo) >= 0.0;
    float3 geometricNormal = frontFace ? vertex.geometricNormal : -vertex.geometricNormal;
    float3 normal = dot(vertex.normal, geometricNormal) >= 0.0 ? vertex.normal : -vertex.normal;

    SurfaceBsdf scattering = {vertex.material, normal, vertex.tangent, wo, frontFace};
    return {scattering, geometricNormal};
}

float3 bdptOffset(BdptVertex vertex, float3 direction)
{
    float side = dot(vertex.geometricNormal, direction) >= 0.0 ? 1.0 : -1.0;

    return vertex.position + vertex.geometricNormal * (side * kRayEpsilon);
}
