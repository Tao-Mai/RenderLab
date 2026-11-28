#ifndef RENDERLAB_SCENE_TYPES_SLANG
#define RENDERLAB_SCENE_TYPES_SLANG

struct GeometryInstanceID
{
    uint index;  // Global instance index.

    __init()
    {
        index = 0;
    }

    __init(uint geometryInstanceID)
    {
        index = geometryInstanceID;
    }
};

enum class GeometryType : uint32_t
{
    None         = 0,
    TriangleMesh = 1,
};

enum class GeometryInstanceFlags : uint32_t
{
    None                = 0x0,
    TransformFlipped    = 0x4,
    IsObjectFrontFaceCW = 0x8,
    IsWorldFrontFaceCW  = 0x10,
};

struct GeometryInstanceData
{
    static const uint kTypeBits   = 3;
    static const uint kTypeOffset = 32 - kTypeBits;

    uint flags;           // Upper kTypeBits store GeometryType; lower bits store GeometryInstanceFlags.
    uint globalMatrixID;  // Index into worldMatrices / inverseTransposeWorldMatrices.
    uint materialID;
    uint geometryID;

    uint vbOffset;        // Vertex offset into global vertex buffer.
    uint ibOffset;        // Index offset (in uint32 indices) into global index buffer.

    uint instanceIndex;   // InstanceIndex in TLAS.
    uint geometryIndex;   // GeometryIndex in BLAS.

    GeometryType getType()
    {
        return GeometryType(flags >> kTypeOffset);
    }

    bool isWorldFrontFaceCW()
    {
        return (flags & uint(GeometryInstanceFlags::IsWorldFrontFaceCW)) != 0;
    }
};

// Matches C++ Vertex (48 bytes) under scalar layout.
struct StaticVertexData
{
    float3 position;
    float3 normal;
    float2 texCrd;
    float4 tangent;
};

#endif
