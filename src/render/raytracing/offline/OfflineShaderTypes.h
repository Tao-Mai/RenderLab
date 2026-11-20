#ifndef RENDERLAB_OFFLINE_SHADER_TYPES_H
#define RENDERLAB_OFFLINE_SHADER_TYPES_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_OFFLINE_CONSTANT inline constexpr uint32_t
#else
#define RENDERLAB_OFFLINE_CONSTANT static const uint
#endif

RENDERLAB_OFFLINE_CONSTANT kOfflineInvalidIndex = 0xffffffffu;
RENDERLAB_OFFLINE_CONSTANT kOfflineLightHasImage = 1u << 0;
RENDERLAB_OFFLINE_CONSTANT kOfflineLightHasPower = 1u << 1;
RENDERLAB_OFFLINE_CONSTANT kOfflineLightHasIlluminance = 1u << 2;

#undef RENDERLAB_OFFLINE_CONSTANT
#endif
