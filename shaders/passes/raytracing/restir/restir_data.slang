#ifndef RENDERLAB_RESTIR_DATA_SLANG
#define RENDERLAB_RESTIR_DATA_SLANG

#include "RestirFrameParameters.h"
#include "raytracing/light_sampling.slang"

static const uint  kRestirMaxSpatialSamples      = 32;
static const float kRestirNormalThreshold        = 0.9;
static const float kRestirRelativeDepthThreshold = 0.1;
static const float kRestirAbsoluteDepthThreshold = 0.001;

uint restirPackDirection(float3 value)
{
    // Octahedral encoding into two signed 16-bit coordinates.
    value /= abs(value.x) + abs(value.y) + abs(value.z);
    float2 folded = value.xy;
    if (value.z < 0.0)
    {
        folded = (1.0 - abs(folded.yx)) * float2(folded.x >= 0.0 ? 1.0 : -1.0, folded.y >= 0.0 ? 1.0 : -1.0);
    }
    uint2 packed = uint2(round(saturate(folded * 0.5 + 0.5) * 65535.0));
    return packed.x | (packed.y << 16);
}

float3 restirUnpackDirection(uint value)
{
    float2 oct    = float2(value & 0xffffu, value >> 16) / 65535.0 * 2.0 - 1.0;
    float3 result = float3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    float  fold   = saturate(-result.z);
    result.xy += float2(result.x >= 0.0 ? -fold : fold, result.y >= 0.0 ? -fold : fold);
    return normalize(result);
}

uint restirPackHalf2(float2 value) { return packHalf2x16(value); }

float2 restirUnpackHalf2(uint value) { return unpackHalf2x16ToFloat(value); }

struct RestirSurfaceData
{
    float4 positionDepth;
    uint4  directions;
    uint4  material;
    uint4  metadata;

    property float3 position
    {
        get { return positionDepth.xyz; }
        set { positionDepth.xyz = newValue; }
    }
    property float depth
    {
        get { return positionDepth.w; }
        set { positionDepth.w = newValue; }
    }
    property bool valid
    {
        get { return depth > 0.0; }
    }
    property float3 normal
    {
        get { return restirUnpackDirection(directions.x); }
        set { directions.x = restirPackDirection(newValue); }
    }
    property float3 geometricNormal
    {
        get { return restirUnpackDirection(directions.y); }
        set { directions.y = restirPackDirection(newValue); }
    }
    property float3 viewDirection
    {
        get { return restirUnpackDirection(directions.z); }
        set { directions.z = restirPackDirection(newValue); }
    }
    property uint materialIndex
    {
        get { return metadata.x; }
        set { metadata.x = newValue; }
    }
    property uint objectIndex
    {
        get { return metadata.y; }
        set { metadata.y = newValue; }
    }
    property float3 tangent
    {
        get { return restirUnpackDirection(metadata.z); }
        set { metadata.z = restirPackDirection(newValue); }
    }
    property bool frontFace
    {
        get { return (metadata.w & 1u) != 0; }
        set { metadata.w = (metadata.w & ~1u) | uint(newValue); }
    }
    property ScatteringMaterial scattering
    {
        get
        {
            ScatteringMaterial result;
            result.coefficients0 = float4(restirUnpackHalf2(material.x), restirUnpackHalf2(material.y));
            result.coefficients1 = float4(restirUnpackHalf2(material.z), restirUnpackHalf2(material.w));
            result.roughness = asfloat(directions.w);
            result.model = ScatteringModel(metadata.w >> 1);
            return result;
        }
        set
        {
            material.x   = restirPackHalf2(newValue.coefficients0.xy);
            material.y   = restirPackHalf2(newValue.coefficients0.zw);
            material.z   = restirPackHalf2(newValue.coefficients1.xy);
            material.w   = restirPackHalf2(newValue.coefficients1.zw);
            directions.w = asuint(newValue.roughness);
            metadata.w = (metadata.w & 1u) | (uint(newValue.model) << 1);
        }
    }
};

struct RestirReservoir
{
    PathLightSample sample;
    float               weightSum;
    float               target;
    uint                sampleCount;
    uint                padding;

    property bool valid{get{return sample.kind != kInvalidLightSample && weightSum > 0.0;
}
}

[mutating] void stream(PathLightSample candidate, float candidateTarget, float weight, uint count, inout uint state)
{
    sampleCount += count;
    weightSum += weight;
    if (randomFloat(state) * weightSum < weight)
    {
        sample = candidate;
        target = candidateTarget;
    }
}

    [mutating] void finalize(float normalization)
{
    weightSum = target > 0.0 && normalization > 0.0 ? weightSum / (target * normalization) : 0.0;
}
}
;

RWStructuredBuffer<RestirSurfaceData> restirSurfaces;
StructuredBuffer<RestirSurfaceData> restirPreviousSurfaces;
RWStructuredBuffer<RestirReservoir> restirInitialReservoirs;
RWStructuredBuffer<RestirReservoir> restirHistory;
ConstantBuffer<RestirFrameParameters> restirFrame;

#endif
