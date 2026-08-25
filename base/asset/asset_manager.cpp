#include "base/asset/asset_manager.h"

#include "base/asset/asset_type.h"
#include "base/asset/scene_asset_json.h"
#include "base/core/scene.h"
#include "base/platform/paths.h"

#include <fstream>
#include <glog/logging.h>

#include <nlohmann/json.hpp>

namespace
{
bool is_hex_asset_id(std::string_view text)
{
    if (text.size() != 16)
        return false;
    for (const char c : text)
    {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex)
            return false;
    }
    return true;
}
}  // namespace

void AssetManager::init()
{
    meshes_.clear();
    scenes_.clear();
    scene_name_index_.clear();
    scene_paths_.clear();
    scene_list_.clear();

    scan_type_folder(AssetType::Mesh);
    scan_type_folder(AssetType::Scene);

    LOG(INFO) << "AssetManager loaded meshes=" << meshes_.size() << " scenes=" << scenes_.size();
}

void AssetManager::scan_type_folder(AssetType type)
{
    const auto dir = paths::assets_dir() / asset_type_folder(type);
    if (!std::filesystem::exists(dir))
    {
        LOG(WARNING) << "Asset folder missing: " << dir;
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        std::ifstream in(entry.path());
        if (!in)
        {
            LOG(WARNING) << "Failed to open asset: " << entry.path();
            continue;
        }

        nlohmann::json j;
        try
        {
            in >> j;
        }
        catch (const std::exception& e)
        {
            LOG(WARNING) << "Invalid asset json " << entry.path() << ": " << e.what();
            continue;
        }

        const std::string asset_type = j.value("type", std::string{});
        if (type == AssetType::Mesh)
        {
            if (asset_type != "mesh")
            {
                LOG(WARNING) << "Skip non-mesh json in mesh/: " << entry.path();
                continue;
            }
            MeshAsset asset = MeshAsset::deserialize(j);
            if (asset.id == kInvalidAssetId)
                asset.id = *parse_asset_id(entry.path().stem().string());
            meshes_.emplace(asset.id, std::move(asset));
        }
        else if (type == AssetType::Scene)
        {
            if (asset_type != "scene")
            {
                LOG(WARNING) << "Skip non-scene json in scene/: " << entry.path();
                continue;
            }
            SceneAsset asset = scene_asset_json::deserialize(j);
            if (asset.id == kInvalidAssetId)
                asset.id = *parse_asset_id(entry.path().stem().string());
            scene_paths_[asset.id] = entry.path();
            if (!asset.name.empty())
                scene_name_index_[asset.name] = asset.id;
            auto [it, _] = scenes_.emplace(asset.id, std::move(asset));
            scene_list_.push_back(&it->second);
        }
    }
}

const MeshAsset* AssetManager::get_mesh(AssetId id) const
{
    const auto it = meshes_.find(id);
    return it == meshes_.end() ? nullptr : &it->second;
}

const SceneAsset* AssetManager::get_scene(AssetId id) const
{
    const auto it = scenes_.find(id);
    return it == scenes_.end() ? nullptr : &it->second;
}

SceneAsset* AssetManager::get_scene_mutable(AssetId id)
{
    const auto it = scenes_.find(id);
    return it == scenes_.end() ? nullptr : &it->second;
}

const SceneAsset* AssetManager::find_scene_by_name(std::string_view name) const
{
    const auto it = scene_name_index_.find(std::string(name));
    if (it == scene_name_index_.end())
        return nullptr;
    return get_scene(it->second);
}

std::string AssetManager::resolve_mesh_source(std::string_view mesh_ref) const
{
    if (mesh_ref.starts_with("builtin:"))
        return std::string(mesh_ref);
    if (is_hex_asset_id(mesh_ref))
    {
        if (const auto id = parse_asset_id(mesh_ref))
        {
            if (const MeshAsset* asset = get_mesh(*id))
                return asset->source;
        }
    }
    return std::string(mesh_ref);
}

bool AssetManager::persist_scene(AssetId id)
{
    const auto it = scenes_.find(id);
    if (it == scenes_.end())
    {
        LOG(ERROR) << "Scene asset not found: " << format_asset_id(id);
        return false;
    }

    const auto path_it = scene_paths_.find(id);
    if (path_it == scene_paths_.end())
    {
        LOG(ERROR) << "Scene asset path not found for id " << format_asset_id(id);
        return false;
    }

    std::ofstream out(path_it->second);
    if (!out)
    {
        LOG(ERROR) << "Failed to open scene asset for write: " << path_it->second;
        return false;
    }
    out << scene_asset_json::serialize(it->second).dump(2);
    if (!it->second.name.empty())
        scene_name_index_[it->second.name] = id;
    LOG(INFO) << "Saved scene asset: " << path_it->second;
    return true;
}

bool AssetManager::save_scene_from_runtime(AssetId id, const Scene& scene)
{
    SceneAsset* asset = get_scene_mutable(id);
    if (!asset)
    {
        LOG(ERROR) << "Cannot save scene, asset missing: " << format_asset_id(id);
        return false;
    }
    if (!scene.sync_to_asset(*asset))
        return false;
    return persist_scene(id);
}

std::filesystem::path AssetManager::scene_asset_path(AssetId id) const
{
    const auto it = scene_paths_.find(id);
    return it == scene_paths_.end() ? std::filesystem::path{} : it->second;
}
