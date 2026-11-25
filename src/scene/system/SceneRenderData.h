#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include "asset/EnvironmentMapAsset.h"
#include "render/DrawItem.h"
#include "scene/system/SceneCamera.h"
#include "scene/system/SceneLight.h"

// A CPU snapshot. Scene extraction never allocates or uploads GPU resources.
struct SceneRenderData
{
    bool operator==(const SceneRenderData&) const = default;

    uint64_t                               sceneGeneration = 0;
    SceneCamera                            camera;
    std::optional<EnvironmentMapAsset::ID> environmentMap;
    std::vector<DrawItem>                  drawItems;
    std::vector<SceneAnalyticLight>        analyticLights;
    std::optional<SceneEnvironmentLight>  environmentLight;
};
