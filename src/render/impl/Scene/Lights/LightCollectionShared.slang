#ifndef RENDERLAB_LIGHT_COLLECTION_SHARED_SLANG
#define RENDERLAB_LIGHT_COLLECTION_SHARED_SLANG

#include "render/resources/SceneLightConstants.h"

struct EmissiveTriangle
{
    float4 p0Area;
    float4 p1;
    float4 p2;
    float4 normal;
    float4 uv01;
    float2 uv2;
    uint materialIndex;
    float power;

    property p0 : float3 { get { return p0Area.xyz; } }
    property vertex1 : float3 { get { return p1.xyz; } }
    property vertex2 : float3 { get { return p2.xyz; } }
    property area : float { get { return p0Area.w; } }
    property geometricNormal : float3 { get { return normal.xyz; } }
    property uv0 : float2 { get { return uv01.xy; } }
    property uv1 : float2 { get { return uv01.zw; } }
};

#endif
