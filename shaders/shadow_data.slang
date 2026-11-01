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
    uint padding;
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

[[vk::binding(0, 3)]] ConstantBuffer<ShadowUniforms> shadowUniforms;
[[vk::binding(1, 3)]] StructuredBuffer<PointShadowData> pointShadowData;
[[vk::binding(2, 3)]] StructuredBuffer<SpotShadowData> spotShadowData;
[[vk::binding(3, 3)]] StructuredBuffer<RectAreaShadowData> rectAreaShadowData;
[[vk::binding(4, 3)]] StructuredBuffer<DirectionalShadowData> directionalShadowData;
[[vk::binding(5, 3)]] TextureCubeArray<float> pointShadowMap;
[[vk::binding(6, 3)]] Texture2DArray<float> spotShadowMap;
[[vk::binding(7, 3)]] Texture2DArray<float> rectAreaShadowMap;
[[vk::binding(8, 3)]] Texture2DArray<float> cascadeShadowMap;
[[vk::binding(9, 3)]] SamplerComparisonState shadowSampler;
[[vk::binding(10, 3)]] SamplerState shadowLinearSampler;

#endif
