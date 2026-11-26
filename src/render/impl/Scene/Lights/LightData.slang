#ifndef RENDERLAB_LIGHT_DATA_SLANG
#define RENDERLAB_LIGHT_DATA_SLANG

enum class LightType : uint {
    Point = 0,
    Directional = 1,
    RectArea = 2,
};

// Must match LightData in ShaderData.h. std430 array stride = 80 bytes; only enabled lights are uploaded.
struct LightData
{
    float3 posW;
    uint lightType;
    float3 dirW;
    float openingAngle;

    // Colored point intensity, directional irradiance, or area-light radiance.
    float3 intensity;
    float cosOpeningAngle;
    float2 areaSize;
    float penumbraAngle;
    float padding0;
    float3 areaRight;
    float padding1;

    property type : LightType {
        get { return LightType(lightType); }
    }
};

float rectLightArea(LightData light)
{
    return light.areaSize.x * light.areaSize.y;
}

float3 sampleRectLightPosition(LightData light, float2 uv)
{
    float2 offset = (uv - 0.5) * light.areaSize;
    float3 up = cross(light.areaRight, light.dirW);

    return light.posW + offset.x * light.areaRight + offset.y * up;
}

#endif
