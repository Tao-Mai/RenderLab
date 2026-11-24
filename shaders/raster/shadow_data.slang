#ifndef RENDERLAB_SHADOW_DATA_SLANG
#define RENDERLAB_SHADOW_DATA_SLANG

struct ShadowUniforms
{
    uint cascadeCount;
    float directionalSampleRadius;
    float bias;
    float sampleRadius;
    float spotSampleRadius;
    uint pixelRotation;
    uint enabled;
    uint linearFilter;
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

#endif
