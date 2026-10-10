#ifndef RENDERLAB_SURFACE_SLANG
#define RENDERLAB_SURFACE_SLANG

struct Surface
{
    float3 worldPosition;
    float3 normal;
    float4 baseColor;
    float roughness;
    float metallic;
    float occlusion;
    float3 emissive;
#if ENABLE_CLEARCOAT
    float3 clearcoatNormal;
    float clearcoatFactor;
    float clearcoatRoughness;
#endif
};

#endif
