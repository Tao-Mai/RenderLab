#ifndef RENDERLAB_LIGHT_DATA_SLANG
#define RENDERLAB_LIGHT_DATA_SLANG

enum class LightType : uint {
    Point = 0,
    Directional = 1,
    RectArea = 2,
    Spot = 3,
};

// Must match LightData in ShaderData.h. std430 array stride = 112 bytes.
// flags = {type, enabled, castShadow, reserved}.
struct LightData
{
    float4 colorIntensity;
    float4 positionRange;
    float4 direction;
    float4 areaSizeCone;
    uint4 flags;
    uint shadowIdx;
    uint padding0;
    uint padding1;
    uint padding2;
    float4 areaRightData;

    property color : float3 {
        get { return colorIntensity.rgb; }
    }
    property intensity : float {
        get { return colorIntensity.w; }
    }
    property radiance : float {
        get { return colorIntensity.w; }
    }
    property position : float3 {
        get { return positionRange.xyz; }
    }
    property range : float {
        get { return positionRange.w; }
    }
    property type : LightType {
        get { return LightType(flags.x); }
    }
    property enabled : bool {
        get { return flags.y != 0; }
    }
    property castShadow : bool {
        get { return flags.z != 0; }
    }
    property areaSize : float2 {
        get { return areaSizeCone.xy; }
    }
    // World-space direction along the rectangle's width (local +X).
    property areaRight : float3 {
        get { return areaRightData.xyz; }
    }
};

#endif
