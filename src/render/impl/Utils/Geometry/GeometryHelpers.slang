#ifndef RENDERLAB_GEOMETRY_HELPERS_SLANG
#define RENDERLAB_GEOMETRY_HELPERS_SLANG

// Ray Tracing Gems, Chapter 6: robust self-intersection avoidance offset along the face normal.
float3 computeRayOrigin(float3 pos, float3 normal)
{
    const float origin = 1.f / 16.f;
    const float fScale = 3.f / 65536.f;
    const float iScale = 3.f * 256.f;

    int3 iOff = int3(normal * iScale);
    float3 iPos = asfloat(asint(pos) + select(pos < 0.f, -iOff, iOff));
    float3 fOff = normal * fScale;

    return select(abs(pos) < origin, pos + fOff, iPos);
}

#endif
