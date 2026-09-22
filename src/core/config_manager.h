#pragma once

#include "core/app_config.h"

#include <filesystem>

class ConfigManager
{
public:
    explicit ConfigManager(std::filesystem::path configFile);

    [[nodiscard]] const AppConfig& config() const noexcept;
    [[nodiscard]] const std::filesystem::path& configFile() const noexcept;
    [[nodiscard]] const std::filesystem::path& assetRoot() const noexcept;
    [[nodiscard]] const AssetId& initialScene() const noexcept;

    void setInitialScene(AssetId id);
    void setAssetRoot(std::filesystem::path root);
    void save() const;
    void reload();

private:
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig data;

    void load();
    void resolvePaths();
};
