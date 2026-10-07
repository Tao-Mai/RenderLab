#ifndef RENDERLAB_SCENE_DATA_SLANG
#define RENDERLAB_SCENE_DATA_SLANG

// Must match ShaderData.h. Matrices use column-major storage; vectors are 16 bytes.
struct ViewUniforms
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float4 cameraPosition;
};

struct LightUniforms
{
    uint lightCount;
    float ambientCoefficient;
    uint padding1;
    uint padding2;
    float4 iblParameters;
    float4 shadowParameters;

    property iblPrefilteredMaxMipLevel : float {
        get { return iblParameters.x; }
    }

    property shadowBias : float {
        get { return shadowParameters.x; }
    }
    property shadowSampleRadius : float {
        get { return shadowParameters.y; }
    }
    property shadowPixelRotation : bool {
        get { return shadowParameters.z != 0.0; }
    }
};

enum class LightType : uint {
    Point = 0,
    Directional = 1,
    RectArea = 2,
    Spot = 3,
};

// std430 array stride = 112 bytes. flags = {type, enabled, castShadow, reserved}.
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
        get {return colorIntensity.w;}
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

struct PointShadowData
{
    float4x4 vp[6];
};

struct RectAreaShadowData
{
    float4x4 vp;
    float nearPlane;
    float farPlane;
    // World-space radius at light-view depth z maps to radius * worldToUvScale / z.
    float2 worldToUvScale;
};

[[vk::binding(0, 0)]]
ConstantBuffer<ViewUniforms> viewUniforms;

[[vk::binding(1, 0)]]
ConstantBuffer<LightUniforms> lightUniforms;

[[vk::binding(2, 0)]]
StructuredBuffer<LightData> lights;

[[vk::binding(3, 0)]]
StructuredBuffer<PointShadowData> pointShadowData;

[[vk::binding(4, 0)]]
StructuredBuffer<RectAreaShadowData> rectAreaShadowData;

#endif
