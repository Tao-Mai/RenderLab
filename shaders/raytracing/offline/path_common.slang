#ifndef RENDERLAB_OFFLINE_PATH_COMMON_SLANG
#define RENDERLAB_OFFLINE_PATH_COMMON_SLANG

#include "scene/scene_data.slang"
#include "material/pbrt/material.slang"
#include "light.slang"
#include "math/low_discrepancy.slang"
#include "math/random.slang"

static const float kMaxRayDistance = 3.402823466e+38;
static const float kRayEpsilon = 0.001;
static const uint kSceneMask = 0x01;
static const uint kVisibilityMissIndex = 1;

struct HitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint4 material; // First index, material index, reverse orientation, first area light.

    property firstIndex : uint {
        get { return material.x; }
    }

    property materialIndex : uint {
        get { return material.y; }
    }

    property reverseOrientation : bool {
        get { return material.z != 0; }
    }

    property firstAreaLight : uint {
        get { return material.w; }
    }
};

struct Hit
{
    uint hit;               // 是否命中
    uint objectIndex;       // 命中物体的索引
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
StructuredBuffer<pbrt::MaterialInstruction> materialInstructions;
StructuredBuffer<pbrt::TextureInstruction> textureInstructions;
RaytracingAccelerationStructure scene;
StructuredBuffer<HitData> objects;
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
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, kSceneMask, 0, 0, kVisibilityMissIndex, ray, visibility);

    return visibility.visible != 0;
}

#endif
