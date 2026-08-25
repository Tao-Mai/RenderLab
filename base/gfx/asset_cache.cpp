#include "base/gfx/asset_cache.h"

#include "base/gfx/mesh_loader.h"

#include <glog/logging.h>

#include <string_view>

AssetCache::AssetCache(std::filesystem::path assets_root) :
    assets_root_(std::move(assets_root))
{
}

const Mesh& AssetCache::get_mesh(const std::string& key)
{
    if (const auto it = meshes_.find(key); it != meshes_.end())
        return it->second;

    auto [it, _] = meshes_.emplace(key, load_mesh(key));
    return it->second;
}

Mesh AssetCache::load_mesh(const std::string& key)
{
    constexpr std::string_view builtin_prefix = "builtin:";
    if (key.starts_with(builtin_prefix))
    {
        const auto name = key.substr(builtin_prefix.size());
        if (name == "plane")
            return MeshFactory::createPlane(1.0f, 1.0f);
        if (name == "cube" || name == "box")
            return MeshFactory::createCuboid(1.0f, 1.0f, 1.0f);
        if (name == "sphere")
            return MeshFactory::createSphere(0.5f);
        LOG(FATAL) << "Unknown builtin mesh: " << key;
    }

    const auto path = assets_root_ / key;
    CHECK(std::filesystem::exists(path)) << "Mesh not found: " << path;
    auto meshes = LoadMeshes(path);
    CHECK(!meshes.empty()) << "No meshes in: " << path;
    return std::move(meshes.front());
}
