#ifndef RENDERLAB_RESTIR_BINDINGS_H
#define RENDERLAB_RESTIR_BINDINGS_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_RESTIR_BINDING inline constexpr uint32_t
#else
#define RENDERLAB_RESTIR_BINDING static const uint
#endif

namespace RestirBinding
{
RENDERLAB_RESTIR_BINDING kSurfaces          = 9;
RENDERLAB_RESTIR_BINDING kPreviousSurfaces  = 10;
RENDERLAB_RESTIR_BINDING kInitialReservoirs = 11;
RENDERLAB_RESTIR_BINDING kReservoirHistory  = 12;
RENDERLAB_RESTIR_BINDING kRestirFrame       = 13;
}

#undef RENDERLAB_RESTIR_BINDING
#endif
