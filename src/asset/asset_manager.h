#pragma once

#include "asset/mesh_data.h"

#include <filesystem>
#include <string>
#include <unordered_map>

class AssetManager
{
  public:
    explicit AssetManager(std::filesystem::path assetRoot = "assets");

    [[nodiscard]] const MeshData &loadMesh(const std::string &identifier);
    [[nodiscard]] std::filesystem::path path(const std::filesystem::path &relative) const;
    void clear();

  private:
    std::filesystem::path root;
    std::unordered_map<std::string, MeshData> meshCache;
};
