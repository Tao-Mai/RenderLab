#include "math/screen.slang"
#ifndef RENDERLAB_SCENE_DATA_SLANG
#define RENDERLAB_SCENE_DATA_SLANG

#include "scene/scene_uniforms.slang"
#include "render/resources/LightRanges.h"

struct SceneGlobals
{
    SceneUniforms camera;
    LightRanges lightRanges;
    float prefilteredMaxLod;
    float padding0;
    float padding1;
    float padding2;
};

StructuredBuffer<SceneGlobals> sceneGlobals;

#endif
