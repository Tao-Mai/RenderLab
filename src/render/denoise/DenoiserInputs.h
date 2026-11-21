#ifndef RENDERLAB_DENOISER_INPUTS_H
#define RENDERLAB_DENOISER_INPUTS_H

#ifdef __cplusplus
#include "render/device/VulkanResources.h"

#define RENDERLAB_LIGHTING_LAYER inline constexpr uint32_t
#else
#define RENDERLAB_LIGHTING_LAYER static const uint
#endif

RENDERLAB_LIGHTING_LAYER kDirectLightingLayer = 0;
RENDERLAB_LIGHTING_LAYER kIndirectLightingLayer = 1;
RENDERLAB_LIGHTING_LAYER kLightingLayerCount = 2;

#undef RENDERLAB_LIGHTING_LAYER

#ifdef __cplusplus
struct DenoiserInputs
{
    VulkanResources::ImageHandle color;
    VulkanResources::ImageHandle depth;
    VulkanResources::ImageHandle worldNormal;
    VulkanResources::ImageHandle emission;
    VulkanResources::ImageHandle albedo;
};
#endif
#endif
