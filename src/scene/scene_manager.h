#pragma once

#include "asset/asset_desc.h"

#include <filesystem>
#include <unordered_map>

class SceneManager
{
public:
    explicit SceneManager(std::filesystem::path assetRoot);

    [[nodiscard]] const SceneDesc& load(const AssetId& id) const;
    void save(const SceneDesc& scene);
    void reload();
    void clear() noexcept;

private:
    std::filesystem::path root;
    std::unordered_map<AssetId, SceneDesc> scenes;
    std::unordered_map<AssetId, std::filesystem::path> descFiles;

    void loadAll();
    [[nodiscard]] std::filesystem::path descFile(const AssetId& id) const;
};
