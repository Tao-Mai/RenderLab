#ifndef RENDERLAB_SCENE_LIGHT_CONSTANTS_H
#define RENDERLAB_SCENE_LIGHT_CONSTANTS_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_SCENE_LIGHT_CONSTANT inline constexpr uint32_t
#else
#define RENDERLAB_SCENE_LIGHT_CONSTANT static const uint
#endif

RENDERLAB_SCENE_LIGHT_CONSTANT kInvalidLightIndex       = 0xffffffffu;
RENDERLAB_SCENE_LIGHT_CONSTANT kEmissiveTwoSided        = 1u << 0;
RENDERLAB_SCENE_LIGHT_CONSTANT kEmissiveTriangleStride  = 96;
RENDERLAB_SCENE_LIGHT_CONSTANT kLightDispatchRowWidth   = 32768;

#undef RENDERLAB_SCENE_LIGHT_CONSTANT
#endif
