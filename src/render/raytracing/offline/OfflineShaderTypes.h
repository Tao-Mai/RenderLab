#ifndef RENDERLAB_OFFLINE_SHADER_TYPES_H
#define RENDERLAB_OFFLINE_SHADER_TYPES_H

#include "render/raytracing/RayTracingBindings.h"

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_OFFLINE_UINT uint32_t
#define RENDERLAB_OFFLINE_CONSTANT inline constexpr uint32_t
#else
#define RENDERLAB_OFFLINE_UINT uint
#define RENDERLAB_OFFLINE_CONSTANT static const uint
#endif

// Shared by the CPU compiler and Slang evaluators.
RENDERLAB_OFFLINE_CONSTANT kOfflineTextureStackSize = 64;
RENDERLAB_OFFLINE_CONSTANT kOfflineMaterialStackSize = 64;
RENDERLAB_OFFLINE_CONSTANT kOfflineInvalidIndex = 0xffffffffu;
RENDERLAB_OFFLINE_CONSTANT kOfflineLightHasImage = 1u << 0;
RENDERLAB_OFFLINE_CONSTANT kOfflineLightHasPower = 1u << 1;
RENDERLAB_OFFLINE_CONSTANT kOfflineLightHasIlluminance = 1u << 2;

enum class OfflineTextureOp : RENDERLAB_OFFLINE_UINT
{
    Program, Constant, Image, Multiply, Mix, Checkerboard, Bilerp, Dots, DirectionMix,
    Fbm, Wrinkled, Windy, Marble, PtexPreview,
};

enum class OfflineTextureMapping : RENDERLAB_OFFLINE_UINT
{
    Uv, Spherical, Cylindrical, Planar, Spatial,
};

enum class OfflineMaterialPreviewModel : RENDERLAB_OFFLINE_UINT
{
    Program, Color, Conductor, DiffuseTransmission, Subsurface, HairAbsorption, HairPigments, Mix,
};

#undef RENDERLAB_OFFLINE_UINT
#undef RENDERLAB_OFFLINE_CONSTANT
#endif
