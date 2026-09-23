#pragma once

#include "asset/asset_desc.h"
#include "asset/asset_id.h"

#include <filesystem>

struct AppPaths
{
    std::filesystem::path assets;
    std::filesystem::path descs;
    std::filesystem::path geometry;
};

struct AppConfig
{
    AppPaths paths;
    AssetId initialScene;
};

class ConfigManager
{
public:
    ConfigManager() = default;

    void init();
    void shutdown() noexcept;

    [[nodiscard]] const AppConfig& config() const noexcept;
    [[nodiscard]] const AppPaths& paths() const noexcept;
    [[nodiscard]] const std::filesystem::path& configFile() const noexcept;
    [[nodiscard]] const std::filesystem::path& assetRoot() const noexcept;
    [[nodiscard]] std::filesystem::path descDir(AssetType type) const;
    [[nodiscard]] const AssetId& initialScene() const noexcept;

    void setInitialScene(AssetId id);
    void setAssetRoot(std::filesystem::path root);
    void save() const;
    void reload();

private:
    bool inited = false;
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig data;

    void load();
    void resolvePaths();
    void applyDefaults();
};
