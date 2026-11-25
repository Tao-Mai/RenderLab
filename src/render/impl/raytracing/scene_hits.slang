#ifndef RENDERLAB_SCENE_HITS_SLANG
#define RENDERLAB_SCENE_HITS_SLANG

static const float kMaxRayDistance = 3.402823466e+38;
static const float kRayEpsilon = 0.001;
static const uint kSceneMask = 0x01;

struct HitData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint firstIndex;
    uint materialIndex;
    uint2 padding;
    float4x4 objectToWorld;
    float4x4 worldToObject;
};

#endif
