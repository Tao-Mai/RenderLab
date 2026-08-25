#pragma once

#include "base/asset/asset_id.h"
#include "base/asset/asset_type.h"

#include <nlohmann/json.hpp>
#include <string>

class MeshAsset
{
public:
    AssetId     id{kInvalidAssetId};
    std::string name;
    std::string source;

    [[nodiscard]] static MeshAsset deserialize(const nlohmann::json& j);
    [[nodiscard]] nlohmann::json serialize() const;

    [[nodiscard]] AssetType type() const { return AssetType::Mesh; }
};
