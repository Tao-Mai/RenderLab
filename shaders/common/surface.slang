#ifndef RENDERLAB_SURFACE_SLANG
#define RENDERLAB_SURFACE_SLANG

#include "common/pbr_brdf.slang"

struct Surface
{
    float3 worldPosition;
    float3 normal;
    PbrBrdf brdf;
    float opacity;
    float occlusion;
    float3 emissive;
#if ENABLE_SHEEN
    float3 sheenColor;
    float sheenRoughness;
#endif
#if ENABLE_CLEARCOAT
    float3 clearcoatNormal;
    float clearcoatFactor;
    float clearcoatRoughness;
#endif
};

#endif
