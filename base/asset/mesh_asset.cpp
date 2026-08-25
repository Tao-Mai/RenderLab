#include "base/asset/mesh_asset.h"

#include <glog/logging.h>
#include <nlohmann/json.hpp>

MeshAsset MeshAsset::deserialize(const nlohmann::json& j)
{
    MeshAsset asset;
    if (const auto id_text = j.value("id", std::string{}); !id_text.empty())
    {
        if (const auto parsed = parse_asset_id(id_text))
            asset.id = *parsed;
        else
            LOG(WARNING) << "Invalid mesh asset id: " << id_text;
    }
    asset.name   = j.value("name", std::string{});
    asset.source = j.value("source", std::string{});
    return asset;
}

nlohmann::json MeshAsset::serialize() const
{
    return nlohmann::json{
        {"id", format_asset_id(id)},
        {"type", "mesh"},
        {"name", name},
        {"source", source},
    };
}
