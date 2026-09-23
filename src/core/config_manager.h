#pragma once

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

    [[nodiscard]] const AppPaths& paths() const noexcept;
    [[nodiscard]] const AssetId& initialScene() const noexcept;

private:
    bool inited = false;
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig data;

    void load();
    void save() const;
    void resolvePaths();
    void applyDefaults();
};
