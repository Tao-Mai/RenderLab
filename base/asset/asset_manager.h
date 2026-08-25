#pragma once

#include "base/asset/mesh_asset.h"
#include "base/asset/scene_asset.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class Scene;

class AssetManager
{
public:
    void init();

    [[nodiscard]] const MeshAsset*  get_mesh(AssetId id) const;
    [[nodiscard]] const SceneAsset* get_scene(AssetId id) const;
    [[nodiscard]] SceneAsset*       get_scene_mutable(AssetId id);
    [[nodiscard]] const SceneAsset* find_scene_by_name(std::string_view name) const;

    [[nodiscard]] const std::vector<SceneAsset*>& list_scenes() const { return scene_list_; }

    [[nodiscard]] std::string resolve_mesh_source(std::string_view mesh_ref) const;

    // 把运行时 Scene 写回内存资产并落盘（无 SceneAsset 整体拷贝）
    bool save_scene_from_runtime(AssetId id, const Scene& scene);

    [[nodiscard]] std::filesystem::path scene_asset_path(AssetId id) const;

private:
    void scan_type_folder(AssetType type);
    bool persist_scene(AssetId id);

    std::unordered_map<AssetId, MeshAsset>  meshes_;
    std::unordered_map<AssetId, SceneAsset> scenes_;
    std::unordered_map<std::string, AssetId> scene_name_index_;
    std::unordered_map<AssetId, std::filesystem::path> scene_paths_;
    std::vector<SceneAsset*> scene_list_;
};
