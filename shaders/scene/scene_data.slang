#ifndef RENDERLAB_SCENE_DATA_SLANG
#define RENDERLAB_SCENE_DATA_SLANG

#include "math/screen.slang"
#include "scene/light_data.slang"
#include "scene/scene_uniforms.slang"
#ifdef SCENE_RAY_TRACING
#include "raytracing/scene_hits.slang"
#endif

struct ViewportRect
{
    float2 offset;
    float2 extent;

    bool containsPixel(uint2 pixel)
    {
        float2 local = float2(pixel) + 0.5 - offset;
        return all(local >= 0.0) && all(local < extent);
    }
};

struct SceneGlobals
{
    SceneUniforms camera;
    uint analyticLightCount;
    float prefilteredMaxLod;
    uint padding0, padding1;
    ViewportRect viewport;
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
