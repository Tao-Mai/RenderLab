#pragma once
#include <glm/mat4x4.hpp>
#include <unordered_map>
#include <vector>
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "render/resource/ShaderData.h"

struct RenderObject
{
    MeshAsset::ID                             mesh;
    std::unordered_map<int, MaterialOverride> materialOverrides;
    glm::mat4                                 model{1.0f};
    uint32_t                                  selectionId     = 0;
    uint32_t                                  componentIndex  = 0;
    float                                     distanceSquared = 0.0f;
};
// A CPU snapshot. Scene extraction never allocates or uploads GPU resources.
struct SceneRenderData
{
    std::vector<RenderObject> objects;
    std::vector<LightData>    lights;
};
