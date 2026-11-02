#ifndef RENDERLAB_OFFLINE_PATH_PARAMETERS_SLANG
#define RENDERLAB_OFFLINE_PATH_PARAMETERS_SLANG

#include "render/raytracing/offline/OfflineShaderTypes.h"

struct PathParameters
{
    float4 viewport;
    uint3 samples; // Completed samples, max bounces, BDPT flags.
    float lightSamples;
    uint4 lightCounts; // Points, spots, directionals, triangles.
    uint4 mappedLightCounts; // Has environment (0/1), projections, goniometrics, portal vertices.

    float4 sceneSphere;
    float4 camera;
    uint4 dispatch;

    property tileOffset : uint2 {
        get { return dispatch.xy; }
    }

    property imageSize : uint2 {
        get { return dispatch.zw; }
    }

    property sceneCenter : float3 {
        get { return sceneSphere.xyz; }
    }

    property sceneRadius : float {
        get { return sceneSphere.w; }
    }

    property cameraForward : float3 {
        get { return camera.xyz; }
    }

    property imagePlaneArea : float {
        get { return camera.w; }
    }

    property lightTracing : bool {
        get { return (samples.z & kOfflineBdptLightTracing) != 0; }
    }

    property powerHeuristic : bool {
        get { return (samples.z & kOfflineBdptPowerHeuristic) != 0; }
    }

    property viewportOffset : float2 {
        get { return viewport.xy; }
    }

    property viewportSize : float2 {
        get { return viewport.zw; }
    }

    property accumulatedSamples : uint {
        get { return samples.x; }
    }

    property maxBounces : uint {
        get { return samples.y; }
    }

    property pointLightCount : uint {
        get { return lightCounts.x; }
    }

    property spotLightCount : uint {
        get { return lightCounts.y; }
    }

    property directionalLightCount : uint {
        get { return lightCounts.z; }
    }

    property triangleLightCount : uint {
        get { return lightCounts.w; }
    }

    property hasEnvironmentLight : bool {
        get { return mappedLightCounts.x != 0; }
    }
};

struct AccumulatedPixel
{
    float3 color;
    uint sampleCount;
};

[[vk::push_constant]] ConstantBuffer<PathParameters> parameters;

#endif
