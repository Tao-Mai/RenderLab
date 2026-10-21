#ifndef RENDERLAB_LIGHTING_DATA_SLANG
#define RENDERLAB_LIGHTING_DATA_SLANG

#include "light_data.slang"

struct LightUniforms
{
    LightRanges lightRanges;
    float ambientCoefficient;
    uint cascadeCount;
    float directionalShadowSampleRadius;
    float padding;
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

[[vk::binding(0, 1)]]
ConstantBuffer<LightUniforms> lightUniforms;

[[vk::binding(1, 1)]]
StructuredBuffer<LightData> lights;

[[vk::binding(2, 1)]]
StructuredBuffer<PointShadowData> pointShadowData;

[[vk::binding(3, 1)]]
StructuredBuffer<SpotShadowData> spotShadowData;

[[vk::binding(4, 1)]]
StructuredBuffer<RectAreaShadowData> rectAreaShadowData;

[[vk::binding(5, 1)]]
StructuredBuffer<DirectionalShadowData> directionalShadowData;

#endif
