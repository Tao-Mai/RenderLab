#ifndef RENDERLAB_RAY_TRACING_BINDINGS_H
#define RENDERLAB_RAY_TRACING_BINDINGS_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_BINDING inline constexpr uint32_t
#else
#define RENDERLAB_BINDING static const uint
#endif

namespace RealtimeRayTracingBinding
{
RENDERLAB_BINDING kTlas      = 0;
RENDERLAB_BINDING kObjects   = 1;
RENDERLAB_BINDING kTriangles = 2;
RENDERLAB_BINDING kColor     = 3;
RENDERLAB_BINDING kDepth     = 4;
RENDERLAB_BINDING kNormal    = 5;
RENDERLAB_BINDING kIndirect  = 6;
RENDERLAB_BINDING kEmission  = 7;
RENDERLAB_BINDING kAlbedo    = 8;
}

namespace OfflineRayTracingBinding
{
RENDERLAB_BINDING kTlas              = 0;
RENDERLAB_BINDING kObjects           = 1;
RENDERLAB_BINDING kMaterials         = 2;
RENDERLAB_BINDING kTextures          = 3;
RENDERLAB_BINDING kAreaEmissions     = 4;
RENDERLAB_BINDING kPoints            = 5;
RENDERLAB_BINDING kSpots             = 6;
RENDERLAB_BINDING kDirectionals      = 7;
RENDERLAB_BINDING kTriangles         = 8;
RENDERLAB_BINDING kEnvironments      = 9;
RENDERLAB_BINDING kProjections       = 10;
RENDERLAB_BINDING kGoniometrics      = 11;
RENDERLAB_BINDING kPortals           = 12;
RENDERLAB_BINDING kAccumulation      = 13;
RENDERLAB_BINDING kColor             = 14;
RENDERLAB_BINDING kSplats            = 15;
RENDERLAB_BINDING kLightDistribution = 16;
}

#undef RENDERLAB_BINDING
#endif
