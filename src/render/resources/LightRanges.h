#ifndef RENDERLAB_LIGHT_RANGES_H
#define RENDERLAB_LIGHT_RANGES_H

#ifdef __cplusplus
#include <cstdint>
#define RENDERLAB_LIGHT_RANGE_UINT uint32_t
#else
#define RENDERLAB_LIGHT_RANGE_UINT uint
#endif

// Exclusive group ends in the light buffer: Directional, Point, RectArea, Spot.
struct LightRanges
{
    RENDERLAB_LIGHT_RANGE_UINT directionalEnd = 0;
    RENDERLAB_LIGHT_RANGE_UINT pointEnd = 0;
    RENDERLAB_LIGHT_RANGE_UINT rectAreaEnd = 0;
    RENDERLAB_LIGHT_RANGE_UINT spotEnd = 0;

#ifdef __cplusplus
    bool operator==(const LightRanges&) const = default;
#endif
};

#ifdef __cplusplus
static_assert(sizeof(LightRanges) == 16);
#endif

#undef RENDERLAB_LIGHT_RANGE_UINT
#endif
