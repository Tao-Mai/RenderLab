#ifndef RENDERLAB_SHADING_DATA_SLANG
#define RENDERLAB_SHADING_DATA_SLANG

#include "Utils/Geometry/GeometryHelpers.slang"
#include "Utils/Math/ShadingFrame.slang"

struct ShadingData
{
    // Geometry data.
    float3       posW;         // Shading hit position in world space.
    float3       V;            // View direction, -incident direction (-ray.dir).
    float2       uv;           // Texture mapping coordinates.

    ShadingFrame frame;        // Smooth interpolated shading frame in world space (not flipped for backfaces).
    float3       faceN;        // Face normal in world space, always on the front-facing side.
    float4       tangentW;     // Geometric tangent (xyz) and sign (w) in world space.
    bool         frontFacing;  // True if primitive seen from the front-facing side, i.e., dot(V, faceN) >= 0.0.

    // Material data.
    uint         materialID;   // Material ID at shading location.
    float        IoR;          // Index of refraction for the medium on the front-facing side ("outside").

    float3 computeRayOrigin(bool viewside = true)
    {
        return ::computeRayOrigin(posW, (frontFacing == viewside) ? faceN : -faceN);
    }

    float3 getOrientedFaceNormal()
    {
        return frontFacing ? faceN : -faceN;
    }
};

#endif
