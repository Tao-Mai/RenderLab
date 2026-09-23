#include "core/config_manager.h"

#include "asset/json_io.h"
#include "logger.h"

#include <utility>

#include <magic_enum/magic_enum.hpp>

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

std::filesystem::path storeRelative(
    const std::filesystem::path& absolute, const std::filesystem::path& baseDir)
{
    const auto relative = std::filesystem::relative(absolute, baseDir);
    if (!relative.empty() && *relative.begin() != "..")
    {
        return relative.generic_string();
    }
    return absolute.generic_string();
}
}

void ConfigManager::init()
{
    if (inited)
    {
        return;
    }
    file = std::filesystem::absolute(RENDERLAB_CONFIG_FILE).lexically_normal();
    configDir = file.parent_path();
    load();
    inited = true;
}

void ConfigManager::shutdown() noexcept
{
    data = {};
    file.clear();
    configDir.clear();
    inited = false;
}

const AppConfig& ConfigManager::config() const noexcept
{
    return data;
}

const AppPaths& ConfigManager::paths() const noexcept
{
    return data.paths;
}

const std::filesystem::path& ConfigManager::configFile() const noexcept
{
    return file;
}

const std::filesystem::path& ConfigManager::assetRoot() const noexcept
{
    return data.paths.assets;
}

std::filesystem::path ConfigManager::descDir(AssetType type) const
{
    return data.paths.descs / magic_enum::enum_name(type);
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
    stored.paths.assets = storeRelative(data.paths.assets, configDir);
    stored.paths.descs = storeRelative(data.paths.descs, data.paths.assets);
    stored.paths.geometry = storeRelative(data.paths.geometry, data.paths.assets);
    asset_json::save(file, stored);
}

void ConfigManager::reload()
{
    CHECK(inited, "ConfigManager is not initialized");
    load();
}

void ConfigManager::applyDefaults()
{
    if (data.paths.assets.empty())
    {
        data.paths.assets = ".";
    }
    if (data.paths.descs.empty())
    {
        data.paths.descs = "descs";
    }
    if (data.paths.geometry.empty())
    {
        data.paths.geometry = "geometry";
    }
    if (data.initialScene == kInvalidAssetId)
    {
        data.initialScene = BuiltinId::defaultScene;
    }
}

void ConfigManager::load()
{
    if (!std::filesystem::exists(file))
    {
        applyDefaults();
        resolvePaths();
        save();
        return;
    }

    data = asset_json::load<AppConfig>(file);
    applyDefaults();
    CHECK(data.initialScene != kInvalidAssetId,
        "config '{}' missing initialScene", file.string());
    resolvePaths();
}

void ConfigManager::resolvePaths()
{
    data.paths.assets = resolveAgainst(configDir, std::move(data.paths.assets));
    CHECK(std::filesystem::is_directory(data.paths.assets),
        "asset root is not a directory: {}", data.paths.assets.string());

    data.paths.descs = resolveAgainst(data.paths.assets, std::move(data.paths.descs));
    data.paths.geometry = resolveAgainst(data.paths.assets, std::move(data.paths.geometry));
}
