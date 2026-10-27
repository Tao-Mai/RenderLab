#pragma once
#include <glm/mat4x4.hpp>
#include <optional>
#include <vector>
#include "asset/EnvironmentMapAsset.h"
#include "render/DrawItem.h"
#include "render/resources/ShaderData.h"

struct RenderLight
{
    bool      operator==(const RenderLight&) const = default;
    LightData data;
    bool      enabled = true;
};

// A CPU snapshot. Scene extraction never allocates or uploads GPU resources.
struct SceneRenderData
{
    bool                                   operator==(const SceneRenderData&) const = default;
    AssetID<struct SceneAsset>             sceneId;
    std::optional<EnvironmentMapAsset::ID> environmentMap;
    std::vector<DrawItem>                  drawItems;
    std::vector<RenderLight>               lights;
};
