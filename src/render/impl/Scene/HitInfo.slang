#ifndef RENDERLAB_HIT_INFO_SLANG
#define RENDERLAB_HIT_INFO_SLANG

#include "Scene/HitInfoType.slang"
#include "Scene/SceneTypes.slang"

// Uncompressed 128-bit Falcor PackedHitInfo layout:
// | packed[0]: type (upper kTypeBits) | instanceID | packed[1]: primitiveIndex | packed[2..3]: barycentrics (fp32) |
typedef uint4 PackedHitInfo;

struct GeometryHit
{
    GeometryInstanceID instanceID;
    uint               primitiveIndex;
    float2             barycentrics;

    float3 getBarycentricWeights()
    {
        return float3(1.f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    }
};

struct TriangleHit : GeometryHit
{
    __init()
    {
        instanceID = GeometryInstanceID();
        primitiveIndex = 0;
        barycentrics = float2(0.f);
    }

    __init(const PackedHitInfo packed)
    {
        HitInfo::unpackHeader(packed, instanceID, primitiveIndex);
        barycentrics.x = asfloat(packed[2]);
        barycentrics.y = asfloat(packed[3]);
    }

    PackedHitInfo pack()
    {
        PackedHitInfo packed = PackedHitInfo(0);
        HitInfo::packHeader(packed, HitType::Triangle, instanceID, primitiveIndex);
        packed[2] = asuint(barycentrics.x);
        packed[3] = asuint(barycentrics.y);

        return packed;
    }
};

struct HitInfo
{
    static const uint kTypeBits       = 2u;
    static const uint kTypeOffset     = 32u - kTypeBits;
    static const uint kInstanceIDMask = (1u << kTypeOffset) - 1u;

    PackedHitInfo data;

    __init()
    {
        data = PackedHitInfo(0);
    }

    __init(const PackedHitInfo packed)
    {
        data = packed;
    }

    __init(const TriangleHit triangleHit)
    {
        data = triangleHit.pack();
    }

    bool isValid()
    {
        return getType() != HitType::None;
    }

    HitType getType()
    {
        return HitType(data[0] >> kTypeOffset);
    }

    TriangleHit getTriangleHit()
    {
        return TriangleHit(data);
    }

    PackedHitInfo pack()
    {
        return data;
    }

    GeometryInstanceID getInstanceID()
    {
        GeometryInstanceID instanceID;
        uint primitiveIndex;
        unpackHeader(data, instanceID, primitiveIndex);

        return instanceID;
    }

    uint getPrimitiveIndex()
    {
        GeometryInstanceID instanceID;
        uint primitiveIndex;
        unpackHeader(data, instanceID, primitiveIndex);

        return primitiveIndex;
    }

    static void packHeader(
        inout PackedHitInfo packed, const HitType type, const GeometryInstanceID instanceID, const uint primitiveIndex)
    {
        packed[0] = (uint(type) << kTypeOffset) | (instanceID.index & kInstanceIDMask);
        packed[1] = primitiveIndex;
    }

    static void unpackHeader(const PackedHitInfo packed, out GeometryInstanceID instanceID, out uint primitiveIndex)
    {
        instanceID = GeometryInstanceID(packed[0] & kInstanceIDMask);
        primitiveIndex = packed[1];
    }
};

HitInfo makeInvalidHit()
{
    return HitInfo();
}

HitInfo unpackHitInfo(PackedHitInfo packedHitInfo)
{
    return HitInfo(packedHitInfo);
}

#endif
