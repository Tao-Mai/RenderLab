#ifndef RENDERLAB_RAY_TYPE_H
#define RENDERLAB_RAY_TYPE_H

// Hit and miss records use the same index for each ray type.
enum class RayType
{
    Scatter = 0,
    Visibility = 1,
    Count,
};

#endif
