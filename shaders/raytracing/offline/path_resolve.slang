#ifndef RENDERLAB_OFFLINE_PATH_RESOLVE_SLANG
#define RENDERLAB_OFFLINE_PATH_RESOLVE_SLANG

#include "path_parameters.slang"
#include "render/raytracing/offline/OfflineRayTracingBindings.h"

[[vk::binding(OfflineRayTracingBinding::kAccumulation, 3)]] StructuredBuffer<AccumulatedPixel> accumulation;
[[vk::binding(OfflineRayTracingBinding::kColor, 3)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;

float3 averageCameraSamples(uint index)
{
    AccumulatedPixel history = accumulation[index];
    return history.sampleCount > 0 ? history.color / float(history.sampleCount) : float3(0.0);
}

#endif
