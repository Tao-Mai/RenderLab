#ifndef RENDERLAB_RESTIR_DATA_SLANG
#define RENDERLAB_RESTIR_DATA_SLANG

static const uint kRestirInvalidLight = 0;
static const uint kRestirAnalyticLight = 1;
static const uint kRestirTriangleLight = 2;
static const uint kRestirEnvironmentLight = 3;
static const uint kRestirMaxSpatialSamples = 32;
static const float kRestirNormalThreshold = 0.9;
static const float kRestirRelativeDepthThreshold = 0.1;
static const float kRestirAbsoluteDepthThreshold = 0.001;
static const float3 kRestirLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

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
    float2 oct = float2(value & 0xffffu, value >> 16) / 65535.0 * 2.0 - 1.0;
    float3 result = float3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    float fold = saturate(-result.z);
    result.xy += float2(result.x >= 0.0 ? -fold : fold, result.y >= 0.0 ? -fold : fold);
    return normalize(result);
}

uint restirPackHalf2(float2 value)
{
    return packHalf2x16(value);
}

float2 restirUnpackHalf2(uint value)
{
    return unpackHalf2x16ToFloat(value);
}

struct RestirSurfaceData
{
    float4 positionDepth;
    uint4 directions;
    uint4 material;
    uint4 metadata;

    property float3 position { get { return positionDepth.xyz; } set { positionDepth.xyz = newValue; } }
    property float depth { get { return positionDepth.w; } set { positionDepth.w = newValue; } }
    property bool valid { get { return depth > 0.0; } }
    property float3 normal { get { return restirUnpackDirection(directions.x); } set { directions.x = restirPackDirection(newValue); } }
    property float3 geometricNormal { get { return restirUnpackDirection(directions.y); } set { directions.y = restirPackDirection(newValue); } }
    property float3 viewDirection { get { return restirUnpackDirection(directions.z); } set { directions.z = restirPackDirection(newValue); } }
    property uint materialIndex { get { return metadata.x; } set { metadata.x = newValue; } }
    property uint objectIndex { get { return metadata.y; } set { metadata.y = newValue; } }
    property PbrBrdf brdf
    {
        get
        {
            float2 d0 = restirUnpackHalf2(material.x);
            float2 d1 = restirUnpackHalf2(material.y);
            float2 s0 = restirUnpackHalf2(material.z);
            float2 s1 = restirUnpackHalf2(material.w);
            PbrBrdf result;
            result.diffuseColor = float3(d0, d1.x);
            result.diffuseFresnelWeight = d1.y;
            result.specularF0 = float3(s0, s1.x);
            result.specularF90 = s1.y;
            result.roughness = asfloat(directions.w);
            return result;
        }
        set
        {
            material.x = restirPackHalf2(newValue.diffuseColor.xy);
            material.y = restirPackHalf2(float2(newValue.diffuseColor.z, newValue.diffuseFresnelWeight));
            material.z = restirPackHalf2(newValue.specularF0.xy);
            material.w = restirPackHalf2(float2(newValue.specularF0.z, newValue.specularF90));
            directions.w = asuint(newValue.roughness);
        }
    }
};

struct RestirLightSample
{
    uint kind;
    uint index;
    float2 uv;
};

struct RestirReservoir
{
    RestirLightSample sample;
    float weightSum;
    float target;
    uint sampleCount;
    uint padding;

    property bool valid { get { return sample.kind != kRestirInvalidLight && weightSum > 0.0; } }

    [mutating]
    void stream(RestirLightSample candidate, float candidateTarget, float weight, uint count, inout uint state)
    {
        sampleCount += count;
        weightSum += weight;
        if (randomFloat(state) * weightSum < weight)
        {
            sample = candidate;
            target = candidateTarget;
        }
    }

    [mutating]
    void finalize(float normalization)
    {
        weightSum = target > 0.0 && normalization > 0.0 ? weightSum / (target * normalization) : 0.0;
    }
};

struct RestirFrameParameters
{
    float4x4 previousViewProjection;
    float4 previousViewport;
    float4 previousDepthRow;
    uint width;
    uint height;
    uint writeLayer;
    uint historyValid;
    uint maxHistoryLength;
    uint spatialSampleCount;
    float spatialRadius;
    uint temporalReuse;
    uint spatialReuse;
    uint padding0;
    uint padding1;
    uint padding2;

    property float2 previousViewportOffset { get { return previousViewport.xy; } }
    property float2 previousViewportExtent { get { return previousViewport.zw; } }
    property uint pixelCount { get { return width * height; } }
};

[[vk::binding(RealtimeRayTracingBinding::kSurfaces, 3)]] RWStructuredBuffer<RestirSurfaceData> restirSurfaces;
[[vk::binding(RealtimeRayTracingBinding::kPreviousSurfaces, 3)]] StructuredBuffer<RestirSurfaceData> restirPreviousSurfaces;
[[vk::binding(RealtimeRayTracingBinding::kInitialReservoirs, 3)]] RWStructuredBuffer<RestirReservoir> restirInitialReservoirs;
[[vk::binding(RealtimeRayTracingBinding::kReservoirHistory, 3)]] RWStructuredBuffer<RestirReservoir> restirHistory;
[[vk::binding(RealtimeRayTracingBinding::kRestirFrame, 3)]] ConstantBuffer<RestirFrameParameters> restirFrame;

#endif
