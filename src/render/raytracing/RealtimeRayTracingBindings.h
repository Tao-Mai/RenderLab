#ifndef RENDERLAB_REALTIME_RAY_TRACING_BINDINGS_H
#define RENDERLAB_REALTIME_RAY_TRACING_BINDINGS_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_BINDING inline constexpr uint32_t
#else
#define RENDERLAB_BINDING static const uint
#endif

namespace RealtimeRayTracingBinding
{
RENDERLAB_BINDING kTlas              = 0;
RENDERLAB_BINDING kObjects           = 1;
RENDERLAB_BINDING kTriangles         = 2;
RENDERLAB_BINDING kColor             = 3;
RENDERLAB_BINDING kDepth             = 4;
RENDERLAB_BINDING kNormal            = 5;
RENDERLAB_BINDING kIndirect          = 6;
RENDERLAB_BINDING kEmission          = 7;
RENDERLAB_BINDING kAlbedo            = 8;
}

#undef RENDERLAB_BINDING
#endif
