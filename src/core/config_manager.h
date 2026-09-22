#pragma once

#include "asset/asset_desc.h"
#include "core/app_config.h"

#include <filesystem>

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
    [[nodiscard]] const std::filesystem::path& descDir(AssetType type) const noexcept;
    [[nodiscard]] const AssetId& initialScene() const noexcept;

    void setInitialScene(AssetId id);
    void setAssetRoot(std::filesystem::path root);
    void save() const;
    void reload();

private:
    bool ready = false;
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig data;

    void load();
    void resolvePaths();
    void applyDefaults();
};
