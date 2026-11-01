#ifndef RENDERLAB_LIGHTING_DATA_SLANG
#define RENDERLAB_LIGHTING_DATA_SLANG

#include "light_data.slang"

struct LightUniforms
{
    LightRanges lightRanges;
    float ambientCoefficient;
    float prefilteredMaxLod;
    float2 padding;
};

[[vk::binding(0, 1)]] ConstantBuffer<LightUniforms> lightUniforms;
[[vk::binding(1, 1)]] StructuredBuffer<LightData> lights;

#endif
