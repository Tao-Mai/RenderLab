#pragma once

#include "base/gfx/mesh.h"

#include <filesystem>
#include <string>
#include <unordered_map>

// 资产缓存：按 key 加载并缓存网格，所有 demo 共用。
class AssetCache
{
public:
    explicit AssetCache(std::filesystem::path assets_root);

    const Mesh& get_mesh(const std::string& key);

private:
    Mesh load_mesh(const std::string& key);

    std::filesystem::path                 assets_root_;
    std::unordered_map<std::string, Mesh> meshes_;
};
