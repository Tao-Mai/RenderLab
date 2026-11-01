#ifndef RENDERLAB_LIGHT_DATA_SLANG
#define RENDERLAB_LIGHT_DATA_SLANG

enum class LightType : uint {
    Point = 0,
    Directional = 1,
    RectArea = 2,
    Spot = 3,
};

// Exclusive group ends; matches LightRanges in ShaderData.h.
struct LightRanges
{
    uint directionalEnd;
    uint pointEnd;
    uint rectAreaEnd;
    uint spotEnd;
};

// Must match LightData in ShaderData.h. std430 array stride = 96 bytes; only enabled lights are uploaded.
struct LightData
{
    // Photometric-scaled RGB: Point/Spot intensity, Directional irradiance at perpendicular incidence,
    // RectArea radiance. Already includes the light's RGB tint and scalar strength.
    float3 color;
    float padding3;
    float4 positionRange;
    float4 direction;
    float4 areaSizeCone;
    uint lightType;
    uint shadowEnabled;
    uint shadowIdx;
    uint padding0;
    float4 areaRightData;

    property position : float3 {
        get { return positionRange.xyz; }
    }
    property range : float {
        get { return positionRange.w; }
    }
    property type : LightType {
        get { return LightType(lightType); }
    }
    property castShadow : bool {
        get { return shadowEnabled != 0; }
    }
    property areaSize : float2 {
        get { return areaSizeCone.xy; }
    }
    property cosInner : float {
        get { return areaSizeCone.z; }
    }
    property cosOuter : float {
        get { return areaSizeCone.w; }
    }
    // World-space direction along the rectangle's width (local +X).
    property areaRight : float3 {
        get { return areaRightData.xyz; }
    }
};

float spotAngularAttenuation(LightData light, float3 lightToSurfaceDirection)
{
    float cosine = dot(light.direction.xyz, lightToSurfaceDirection);
    // Equal inner/outer angles describe a hard-edged cone.
    if (light.cosInner <= light.cosOuter)
        return cosine >= light.cosOuter ? 1.0 : 0.0;
    float attenuation = saturate((cosine - light.cosOuter) / (light.cosInner - light.cosOuter));
    return attenuation * attenuation;
}

#endif
