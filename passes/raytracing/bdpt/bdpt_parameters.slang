#ifndef RENDERLAB_BDPT_PARAMETERS_SLANG
#define RENDERLAB_BDPT_PARAMETERS_SLANG

#include "BdptBindings.h"
#include "raytracing/accumulation/path_parameters.slang"

struct BdptParameters
{
    PathParameters path;
    float4 sceneSphere;
    float4 camera;
    uint2 tileOffset;
    float lightSamples;
    uint flags;

    property float3 sceneCenter { get { return sceneSphere.xyz; } }
    property float sceneRadius { get { return sceneSphere.w; } }
    property float3 cameraForward { get { return camera.xyz; } }
    property float imagePlaneArea { get { return camera.w; } }
    property bool lightTracing { get { return (flags & BdptBinding::kLightTracing) != 0; } }
    property bool powerHeuristic { get { return (flags & BdptBinding::kPowerHeuristic) != 0; } }
};

ConstantBuffer<BdptParameters> parameters;

#endif
