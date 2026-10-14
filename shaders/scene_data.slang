#ifndef RENDERLAB_SCENE_DATA_SLANG
#define RENDERLAB_SCENE_DATA_SLANG

#include "light_data.slang"

// Must match ShaderData.h. Matrices use column-major storage; vectors are 16 bytes.
struct ViewUniforms
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4x4 view;
};

struct LightUniforms
{
    uint lightCount;
    float ambientCoefficient;
    uint cascadeCount;
    float directionalShadowSampleRadius;
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
    property spotShadowSampleRadius : float {
        get { return shadowParameters.w; }
    }
    property shadowPixelRotation : bool {
        get { return shadowParameters.z != 0.0; }
    }
};

struct PointShadowData
{
    float4x4 vp[6];
};

struct SpotShadowData
{
    float4x4 vp;
};

struct RectAreaShadowData
{
    float4x4 vp;
    float nearPlane;
    float farPlane;
    // World-space radius at light-view depth z maps to radius * worldToUvScale / z.
    float2 worldToUvScale;
};

// Must match kMaxDirectionalShadowCascades in RenderConfig.h.
static const uint kMaxDirectionalShadowCascades = 4;

struct CascadeShadowData
{
    float4x4 vp;
    float depthUpperBound;
    float blendStartDepth;
    float padding1;
    float padding2;
};

struct DirectionalShadowData
{
    CascadeShadowData cascades[kMaxDirectionalShadowCascades];
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
StructuredBuffer<SpotShadowData> spotShadowData;

[[vk::binding(5, 0)]]
StructuredBuffer<RectAreaShadowData> rectAreaShadowData;

[[vk::binding(6, 0)]]
StructuredBuffer<DirectionalShadowData> directionalShadowData;

#endif
