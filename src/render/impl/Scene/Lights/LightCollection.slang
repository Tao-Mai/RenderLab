#ifndef RENDERLAB_LIGHT_COLLECTION_SLANG
#define RENDERLAB_LIGHT_COLLECTION_SLANG

#include "Scene/Lights/LightCollectionShared.slang"

struct LightCollection
{
    uint triangleCount;
    StructuredBuffer<uint> instanceOffsets;
    StructuredBuffer<EmissiveTriangle> triangles;

    uint getTriangleCount() { return triangleCount; }

    uint triangleIndex(uint instanceIndex, uint primitiveIndex)
    {
        if (triangleCount == 0)
            return kInvalidLightIndex;

        uint offset = instanceOffsets[instanceIndex];

        return offset == kInvalidLightIndex ? kInvalidLightIndex : offset + primitiveIndex;
    }
};

ParameterBlock<LightCollection> emissiveLights;

#endif
