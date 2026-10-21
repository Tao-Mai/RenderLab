#pragma once

#include "render/feature/shadow/ShadowData.h"

#include <vector>

class AssetManager;
class Profiler;
struct SceneRenderData;

struct ObjectVisibility
{
    float    distanceSquared = 0.0f;
    uint32_t visibilityMask  = 0;
};

struct PreparedScene
{
    std::vector<ObjectVisibility> objects;
    ShadowFrameData               lighting;
};

PreparedScene prepareRenderScene(
    const SceneRenderData&       scene,
    const SceneUniforms&         view,
    const RenderFeature::Shadow& shadows,
    AssetManager&                assets,
    Profiler*                    profiler
);
