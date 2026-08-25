#pragma once

#include "base/asset/asset_id.h"
#include "base/asset/asset_type.h"
#include "base/io/app_config.h"

#include <entt/meta/meta.hpp>

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct EntityDesc
{
    std::string                                  name;
    std::unordered_map<std::string, entt::meta_any> components;
};

class SceneAsset
{
public:
    AssetId     id{kInvalidAssetId};
    std::string name;
    glm::vec3   clear_color{0.08f, 0.09f, 0.12f};
    glm::vec3   ambient{0.08f, 0.08f, 0.08f};
    std::optional<SceneCameraConfig> camera;
    std::vector<EntityDesc>          entities;

    [[nodiscard]] AssetType type() const { return AssetType::Scene; }
};
