#ifndef RENDERLAB_ACCUMULATION_PATH_COMMON_SLANG
#define RENDERLAB_ACCUMULATION_PATH_COMMON_SLANG

#include "scene/scene_data.slang"
#include "material/scattering.slang"
#include "scene/light_emitters.slang"
#include "raytracing/scene_hits.slang"
#include "math/low_discrepancy.slang"
#include "math/random.slang"

struct Hit
{
    uint hit;               // 是否命中
    uint emitterOnly;
    uint materialIndex;
    uint areaLightIndex;
    uint primitiveIndex;    // 命中物体的三角形索引，用内置函数得到
    float2 uv;              // 命中物体的纹理坐标
    float3 position;        // 命中物体的世界空间位置
    float3 normal;          // 着色法线，插值得到
    float3 geometricNormal; // 三角形真实法线，叉乘得到
    float3 tangent;
};

struct Visibility
{
    uint visible;
};

#include "path_parameters.slang"
RWStructuredBuffer<AccumulatedPixel> accumulation;

bool visible(float3 origin, float3 direction, float distance)
{
    if (distance <= kRayEpsilon)
    {
        return false;
    }

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = distance - kRayEpsilon;
    Visibility visibility = {};
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        kSceneMask | kLightMask, 0, 0, kVisibilityMissIndex, ray, visibility);

    return visibility.visible != 0;
}

#endif
