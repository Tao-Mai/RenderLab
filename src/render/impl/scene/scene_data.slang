#ifndef RENDERLAB_SCENE_DATA_SLANG
#define RENDERLAB_SCENE_DATA_SLANG

#include "math/screen.slang"
#include "raytracing/scene_hits.slang"
#include "scene/light_data.slang"
#include "scene/scene_uniforms.slang"
#include "render/resources/RayType.h"

struct SceneData
{
    // These uniform fields match SceneGlobals in ShaderData.h.
    SceneUniforms camera;
    uint analyticLightCount;
    uint padding0, padding1, padding2;

    StructuredBuffer<LightData> analyticLights;

    RaytracingAccelerationStructure rtAccel;
    StructuredBuffer<HitData> objects;
};

ParameterBlock<SceneData> scene;

void traceSceneRay<Payload>(RayDesc ray, RayType type, uint flags, inout Payload payload)
{
    const uint index = uint(type);
    // All instances and geometries share the same hit-group records.
    TraceRay(scene.rtAccel, flags, kSceneMask, index, 0, index, ray, payload);
}

#endif
