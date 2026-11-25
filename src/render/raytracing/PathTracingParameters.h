#ifndef RENDERLAB_PATH_TRACING_PARAMETERS_H
#define RENDERLAB_PATH_TRACING_PARAMETERS_H

#ifdef __cplusplus
#include "core/MetadataAnnotations.h"

#include <cstdint>

inline constexpr uint32_t kMinPathBounces = 1;
inline constexpr uint32_t kMaxPathBounces = 64;

#define RENDERLAB_PATH_ALIGN alignas(16) RL_SHADER_LAYOUT
#define RENDERLAB_PATH_UINT uint32_t
#else
#define RENDERLAB_PATH_ALIGN
#define RENDERLAB_PATH_UINT uint
#endif

struct RENDERLAB_PATH_ALIGN PathTracingParameters
{
    RENDERLAB_PATH_UINT maxBounces;
    RENDERLAB_PATH_UINT sampleIndex;
    RENDERLAB_PATH_UINT padding0;
    RENDERLAB_PATH_UINT padding1;
};

#undef RENDERLAB_PATH_ALIGN
#undef RENDERLAB_PATH_UINT

#endif
