#pragma once
#include <glm/mat4x4.hpp>
#include <optional>
#include <unordered_map>
#include <vector>
#include "asset/EnvironmentMapAsset.h"
#include "asset/MeshAsset.h"
#include "asset/PbrMaterialAsset.h"
#include "render/resource/ShaderData.h"

struct RenderObject
{
    bool                                      operator==(const RenderObject&) const = default;
    MeshAsset::ID                             mesh;
    std::unordered_map<int, MaterialOverride> materialOverrides;
    glm::mat4                                 model{1.0f};
    uint32_t                                  selectionId   = 0;
    uint32_t                                  instanceIndex = 0;
};
struct RenderLight
{
    bool operator==(const RenderLight&) const = default;
    LightData data;
    bool enabled = true;
};

// A CPU snapshot. Scene extraction never allocates or uploads GPU resources.
struct SceneRenderData
{
    bool                                   operator==(const SceneRenderData&) const = default;
    AssetID<struct SceneAsset>             sceneId;
    std::optional<EnvironmentMapAsset::ID> environmentMap;
    std::vector<RenderObject>              objects;
    std::vector<RenderLight>               lights;
};
