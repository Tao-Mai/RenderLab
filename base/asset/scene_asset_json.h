#pragma once

#include "base/asset/scene_asset.h"

#include <nlohmann/json.hpp>

namespace scene_asset_json
{
[[nodiscard]] SceneAsset deserialize(const nlohmann::json& j);
[[nodiscard]] nlohmann::json serialize(const SceneAsset& asset);
}
