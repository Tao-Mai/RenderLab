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
    uint padding0;
    uint padding1;
    uint padding2;
    float4 iblParameters;

    property iblPrefilteredMaxMipLevel : float {
        get { return iblParameters.x; }
    }
};

// std430 array stride = 80 bytes. flags = {type, enabled, castShadow, reserved}.
struct LightData
{
    float4 colorIntensity;
    float4 positionRange;
    float4 direction;
    float4 areaSizeCone;
    uint4 flags;

    property position : float3 {
        get { return positionRange.xyz; }
    }
    property range : float {
        get { return positionRange.w; }
    }
};

[[vk::binding(0, 0)]]
ConstantBuffer<ViewUniforms> viewUniforms;

[[vk::binding(11, 0)]]
ConstantBuffer<LightUniforms> lightUniforms;

[[vk::binding(12, 0)]]
StructuredBuffer<LightData> lights;

#endif
