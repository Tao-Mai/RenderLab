#ifndef RENDERLAB_SCENE_DATA_SLANG
#define RENDERLAB_SCENE_DATA_SLANG

#include "math/screen.slang"
#include "scene/light_data.slang"
#include "scene/scene_uniforms.slang"
#ifdef SCENE_RAY_TRACING
#include "raytracing/scene_hits.slang"
#endif

struct SceneGlobals
{
    SceneUniforms camera;
    uint analyticLightCount;
    float prefilteredMaxLod;
    uint padding0, padding1;
};

struct SceneData
{
    StructuredBuffer<SceneGlobals> globals;
    StructuredBuffer<LightData> analyticLights;
#ifdef SCENE_RAY_TRACING
    RaytracingAccelerationStructure rtAccel;
    StructuredBuffer<HitData> objects;
#endif
};

ParameterBlock<SceneData> scene;

#endif
