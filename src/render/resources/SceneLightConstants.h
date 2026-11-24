#ifndef RENDERLAB_SCENE_LIGHT_CONSTANTS_H
#define RENDERLAB_SCENE_LIGHT_CONSTANTS_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_SCENE_LIGHT_CONSTANT inline constexpr uint32_t
#else
#define RENDERLAB_SCENE_LIGHT_CONSTANT static const uint
#endif

RENDERLAB_SCENE_LIGHT_CONSTANT kInvalidLightIndex       = 0xffffffffu;
RENDERLAB_SCENE_LIGHT_CONSTANT kLightHasImage           = 1u << 0;
RENDERLAB_SCENE_LIGHT_CONSTANT kLightHasPower           = 1u << 1;
RENDERLAB_SCENE_LIGHT_CONSTANT kLightHasIlluminance     = 1u << 2;
RENDERLAB_SCENE_LIGHT_CONSTANT kLightFlipUv             = 1u << 3;
RENDERLAB_SCENE_LIGHT_CONSTANT kLightMultiplyColor      = 1u << 4;
RENDERLAB_SCENE_LIGHT_CONSTANT kLightTriangleHeaderSize = 16;
RENDERLAB_SCENE_LIGHT_CONSTANT kLightTriangleStride     = 96;

#undef RENDERLAB_SCENE_LIGHT_CONSTANT
#endif
