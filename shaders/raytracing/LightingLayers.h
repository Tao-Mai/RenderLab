#ifndef RENDERLAB_LIGHTING_LAYERS_H
#define RENDERLAB_LIGHTING_LAYERS_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_LIGHTING_LAYER inline constexpr uint32_t
#else
#define RENDERLAB_LIGHTING_LAYER static const uint
#endif

RENDERLAB_LIGHTING_LAYER kDirectLightingLayer   = 0;
RENDERLAB_LIGHTING_LAYER kIndirectLightingLayer = 1;
RENDERLAB_LIGHTING_LAYER kLightingLayerCount    = 2;

#undef RENDERLAB_LIGHTING_LAYER

#endif
