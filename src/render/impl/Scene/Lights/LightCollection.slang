#ifndef RENDERLAB_LIGHT_COLLECTION_SLANG
#define RENDERLAB_LIGHT_COLLECTION_SLANG

#include "Scene/Lights/LightCollectionShared.slang"
#include "Scene/SceneTypes.slang"

struct LightCollection
{
    static const uint kInvalidIndex = 0xffffffffu;

    uint                                     triangleCount;          // Total number of emissive triangles.
    StructuredBuffer<PackedEmissiveTriangle> triangleData;           // Per-triangle geometry data.
    StructuredBuffer<uint>                   perMeshInstanceOffset;  // Per-instance offset into triangleData, or kInvalidIndex.

    uint getTriangleCount() { return triangleCount; }
    bool isEmpty() { return triangleCount == 0; }

    EmissiveTriangle getTriangle(uint triIdx)
    {
        return triangleData[triIdx].unpack();
    }

    uint getTriangleIndex(GeometryInstanceID instanceID, uint primitiveIndex)
    {
        if (triangleCount == 0)
            return kInvalidIndex;

        uint offset = perMeshInstanceOffset[instanceID.index];

        return offset != kInvalidIndex ? offset + primitiveIndex : kInvalidIndex;
    }
};

#endif
