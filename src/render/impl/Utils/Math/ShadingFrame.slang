#ifndef RENDERLAB_SHADING_FRAME_SLANG
#define RENDERLAB_SHADING_FRAME_SLANG

// Stark (2009) branchless perpendicular unit vector.
float3 perp_stark(float3 u)
{
    float3 a = abs(u);
    uint uyx = (a.x - a.y) < 0.f ? 1u : 0u;
    uint uzx = (a.x - a.z) < 0.f ? 1u : 0u;
    uint uzy = (a.y - a.z) < 0.f ? 1u : 0u;
    uint xm = uyx & uzx;
    uint ym = (1u ^ xm) & uzy;
    uint zm = 1u ^ (xm | ym);

    return normalize(cross(u, float3(xm, ym, zm)));
}

struct ShadingFrame
{
    float3 T;  // Shading tangent. Normalized.
    float3 B;  // Shading bitangent. Normalized.
    float3 N;  // Shading normal. Normalized.

    __init()
    {
        T = float3(0.f);
        B = float3(0.f);
        N = float3(0.f);
    }

    __init(const float3 normalW)
    {
        N = normalize(normalW);
        T = perp_stark(N);
        B = cross(N, T);
    }

    __init(const float3 normalW, const float4 tangentW)
    {
        N = normalize(normalW);
        orthonormalize(tangentW);
    }

    static ShadingFrame createSafe(const float3 normalW, const float4 tangentW, out bool valid)
    {
        ShadingFrame sf;
        sf.N = normalize(normalW);
        valid = sf.orthonormalizeSafe(tangentW);

        return sf;
    }

    float3 fromLocal(const float3 v)
    {
        return T * v.x + B * v.y + N * v.z;
    }

    float3 toLocal(const float3 v)
    {
        return float3(dot(v, T), dot(v, B), dot(v, N));
    }

    [mutating]
    void orthonormalize(const float4 tangentW)
    {
        T = normalize(tangentW.xyz - N * dot(tangentW.xyz, N));
        B = cross(N, T) * tangentW.w;
    }

    [mutating]
    bool orthonormalizeSafe(const float4 tangentW)
    {
        float NdotT = dot(tangentW.xyz, N);
        bool nonParallel = abs(NdotT) < 0.9999f;
        bool nonZero = dot(tangentW.xyz, tangentW.xyz) > 0.f;
        bool valid = abs(tangentW.w) == 1.f && nonZero && nonParallel;

        if (valid)
        {
            T = normalize(tangentW.xyz - N * NdotT);
            B = cross(N, T) * tangentW.w;
        }
        else
        {
            T = perp_stark(N);
            B = cross(N, T);
        }

        return valid;
    }
};

#endif
