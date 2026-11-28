#ifndef RENDERLAB_LIGHT_DATA_SLANG
#define RENDERLAB_LIGHT_DATA_SLANG

enum class LightType : uint
{
    Point       = 0,  // Point light source, can be a spot light if its opening angle is < pi.
    Directional = 1,  // Directional light source.
    Rect        = 2,  // Quad shaped area light source.
};

// Must match LightData in ShaderData.h. std430 array stride = 192 bytes; only enabled lights are uploaded.
struct LightData
{
    float3   posW;             // World-space position of the center of a light source.
    uint     type;             // Type of the light source (LightType).
    float3   dirW;             // World-space orientation of the light source (normalized).
    float    openingAngle;     // Spot light opening half-angle in radians (pi for full sphere).
    float3   intensity;        // Point radiant intensity, directional irradiance, or area-light radiance.
    float    cosOpeningAngle;  // cos(openingAngle).
    float    penumbraAngle;    // Spot light penumbra half-angle in radians.
    float    surfaceArea;      // Surface area of the analytic area light.
    float2   _pad0;

    // Extra parameters for analytic area lights.
    float4x4 transMat;         // Transformation matrix from local [-1, 1]^2 to world space.
    float4x4 transMatIT;       // Inverse-transpose of transformation matrix.
};

#endif
