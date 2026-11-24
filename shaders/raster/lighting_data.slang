#ifndef RENDERLAB_LIGHTING_DATA_SLANG
#define RENDERLAB_LIGHTING_DATA_SLANG

#include "scene/light_data.slang"

struct LightUniforms
{
    LightRanges lightRanges;
    float ambientCoefficient;
    float prefilteredMaxLod;
    float2 padding;
};

ConstantBuffer<LightUniforms> lightUniforms;
StructuredBuffer<LightData> lights;

#endif
