#ifndef RENDERLAB_EMISSIVE_LIGHT_DATA_SLANG
#define RENDERLAB_EMISSIVE_LIGHT_DATA_SLANG

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

struct EmissiveLights
{
    uint triangleCount;
    StructuredBuffer<uint> instanceOffsets;
    StructuredBuffer<EmissiveTriangle> triangles;

    uint triangleIndex(uint instanceIndex, uint primitiveIndex)
    {
        if (triangleCount == 0)
            return kInvalidLightIndex;
        uint offset = instanceOffsets[instanceIndex];
        return offset == kInvalidLightIndex ? kInvalidLightIndex : offset + primitiveIndex;
    }
};

#endif
