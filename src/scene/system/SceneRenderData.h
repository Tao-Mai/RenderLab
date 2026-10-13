#pragma once
#include <glm/mat4x4.hpp>
#include <unordered_map>
#include <optional>
#include <vector>
#include "asset/PbrMaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/EnvironmentMapAsset.h"
#include "render/resource/ShaderData.h"
#include "render/feature/shadow/ShadowData.h"

struct RenderObject
{
    bool operator==(const RenderObject&) const = default;
    MeshAsset::ID                             mesh;
    std::unordered_map<int, MaterialOverride> materialOverrides;
    glm::mat4                                 model{1.0f};
    uint32_t                                  selectionId     = 0;
    uint32_t                                  componentIndex  = 0;
    float                                     distanceSquared = 0.0f;
    uint32_t                                  visibilityMask = 0;
};
// A CPU snapshot. Scene extraction never allocates or uploads GPU resources.
struct SceneRenderData
{
    bool operator==(const SceneRenderData&) const = default;
    AssetID<struct SceneAsset> sceneId;
    std::optional<EnvironmentMapAsset::ID> environmentMap;
    std::vector<RenderObject> objects;
    ShadowFrameData          lighting;
};
