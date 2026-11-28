#ifndef RENDERLAB_VERTEX_DATA_SLANG
#define RENDERLAB_VERTEX_DATA_SLANG

// Interpolated vertex attributes in world space.
struct VertexData
{
    float3 posW;         // Position in world space.
    float3 normalW;      // Shading normal in world space (normalized).
    float4 tangentW;     // Shading tangent in world space (normalized). w is +-1.0 or 0 if missing.
    float2 texC;         // Texture coordinate.
    float3 faceNormalW;  // Face normal in world space (normalized).
};

#endif
