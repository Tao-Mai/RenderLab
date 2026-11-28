#ifndef RENDERLAB_LIGHT_COLLECTION_SHARED_SLANG
#define RENDERLAB_LIGHT_COLLECTION_SHARED_SLANG

struct EmissiveTriangle
{
    float3 posW[3];       // Per-vertex world-space positions.
    float2 texCoords[3];  // Per-vertex texture coordinates.
    float3 normal;        // Face normal in world space.
    float  area;          // Triangle area in world space.
    uint   materialID;    // Material ID.

    float3 getPosition(float3 barycentrics)
    {
        return posW[0] * barycentrics[0] +
               posW[1] * barycentrics[1] +
               posW[2] * barycentrics[2];
    }

    float2 getTexCoord(float3 barycentrics)
    {
        return texCoords[0] * barycentrics[0] +
               texCoords[1] * barycentrics[1] +
               texCoords[2] * barycentrics[2];
    }
};

// Packed per-triangle geometry data for emissive triangles (96 bytes, matches kEmissiveTriangleStride).
struct PackedEmissiveTriangle
{
    float4 pos0Area;    // xyz: posW[0], w: area.
    float4 pos1;        // xyz: posW[1].
    float4 pos2;        // xyz: posW[2].
    float4 normal;      // xyz: face normal in world space.
    float4 texCoord01;  // xy: texCoords[0], zw: texCoords[1].
    float2 texCoord2;   // texCoords[2].
    uint   materialID;  // Material ID.
    uint   _pad;

    [mutating] void pack(const EmissiveTriangle tri)
    {
        pos0Area   = float4(tri.posW[0], tri.area);
        pos1       = float4(tri.posW[1], 0.0);
        pos2       = float4(tri.posW[2], 0.0);
        normal     = float4(tri.normal, 0.0);
        texCoord01 = float4(tri.texCoords[0], tri.texCoords[1]);
        texCoord2  = tri.texCoords[2];
        materialID = tri.materialID;
        _pad       = 0;
    }

    EmissiveTriangle unpack()
    {
        EmissiveTriangle tri;
        tri.posW[0]      = pos0Area.xyz;
        tri.posW[1]      = pos1.xyz;
        tri.posW[2]      = pos2.xyz;
        tri.texCoords[0] = texCoord01.xy;
        tri.texCoords[1] = texCoord01.zw;
        tri.texCoords[2] = texCoord2;
        tri.normal       = normal.xyz;
        tri.area         = pos0Area.w;
        tri.materialID   = materialID;

        return tri;
    }
};

#endif
