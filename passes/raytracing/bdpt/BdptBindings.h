#ifndef RENDERLAB_BDPT_BINDINGS_H
#define RENDERLAB_BDPT_BINDINGS_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_BDPT_CONSTANT inline constexpr uint32_t
#else
#define RENDERLAB_BDPT_CONSTANT static const uint
#endif

namespace BdptBinding
{
RENDERLAB_BDPT_CONSTANT kLightTracing   = 1u << 0;
RENDERLAB_BDPT_CONSTANT kPowerHeuristic = 1u << 1;
}  // namespace BdptBinding

#undef RENDERLAB_BDPT_CONSTANT
#endif
