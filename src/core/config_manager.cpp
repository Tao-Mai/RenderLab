#include "core/config_manager.h"

#include "asset/json_io.h"
#include "logger.h"

#include <utility>

namespace
{
std::filesystem::path resolveAgainst(
    const std::filesystem::path& baseDir, std::filesystem::path value)
{
    if (value.empty())
    {
        return baseDir;
    }
    if (value.is_absolute())
    {
        return value.lexically_normal();
    }
    return (baseDir / value).lexically_normal();
}
}

ConfigManager::ConfigManager(std::filesystem::path configFile) :
    file(std::filesystem::absolute(std::move(configFile)).lexically_normal()),
    configDir(file.parent_path())
{
    load();
}

const AppConfig& ConfigManager::config() const noexcept
{
    return data;
}

const std::filesystem::path& ConfigManager::configFile() const noexcept
{
    return file;
}

const std::filesystem::path& ConfigManager::assetRoot() const noexcept
{
    return data.paths.assets;
}

const AssetId& ConfigManager::initialScene() const noexcept
{
    return data.initialScene;
}

void ConfigManager::setInitialScene(AssetId id)
{
    data.initialScene = std::move(id);
}

void ConfigManager::setAssetRoot(std::filesystem::path root)
{
    data.paths.assets = std::filesystem::absolute(std::move(root)).lexically_normal();
}

void ConfigManager::save() const
{
    AppConfig stored = data;
    const auto absoluteAssets = std::filesystem::absolute(data.paths.assets).lexically_normal();
    const auto relativeAssets = std::filesystem::relative(absoluteAssets, configDir);
    if (!relativeAssets.empty() && *relativeAssets.begin() != "..")
    {
        stored.paths.assets = relativeAssets.generic_string();
    }
    else
    {
        stored.paths.assets = absoluteAssets.generic_string();
    }
    asset_json::save(file, stored);
}

void ConfigManager::reload()
{
    load();
}

void ConfigManager::load()
{
    if (!std::filesystem::exists(file))
    {
        data.paths.assets = ".";
        data.initialScene = "scene:default";
        resolvePaths();
        save();
        return;
    }

    data = asset_json::load<AppConfig>(file);
    CHECK(!data.initialScene.empty(), "config '{}' missing initialScene", file.string());
    resolvePaths();
}

void ConfigManager::resolvePaths()
{
    data.paths.assets = resolveAgainst(configDir, std::move(data.paths.assets));
    CHECK(std::filesystem::is_directory(data.paths.assets),
        "asset root is not a directory: {}", data.paths.assets.string());
}
