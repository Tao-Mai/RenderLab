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
    if (ready)
    {
        return;
    }
    file = std::filesystem::absolute(RENDERLAB_CONFIG_FILE).lexically_normal();
    configDir = file.parent_path();
    load();
    ready = true;
}

void ConfigManager::shutdown() noexcept
{
    data = {};
    file.clear();
    configDir.clear();
    ready = false;
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

const std::filesystem::path& ConfigManager::descDir(AssetType type) const noexcept
{
    switch (type)
    {
    case AssetType::Mesh:
        return data.paths.meshDescs;
    case AssetType::Material:
        return data.paths.materialDescs;
    case AssetType::Texture:
        return data.paths.textureDescs;
    case AssetType::Shader:
        return data.paths.shaderDescs;
    case AssetType::Scene:
        return data.paths.sceneDescs;
    }
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
    stored.paths.assets = storeRelative(data.paths.assets, configDir);
    stored.paths.meshDescs = storeRelative(data.paths.meshDescs, data.paths.assets);
    stored.paths.materialDescs = storeRelative(data.paths.materialDescs, data.paths.assets);
    stored.paths.textureDescs = storeRelative(data.paths.textureDescs, data.paths.assets);
    stored.paths.shaderDescs = storeRelative(data.paths.shaderDescs, data.paths.assets);
    stored.paths.sceneDescs = storeRelative(data.paths.sceneDescs, data.paths.assets);
    stored.paths.geometry = storeRelative(data.paths.geometry, data.paths.assets);
    asset_json::save(file, stored);
}

void ConfigManager::reload()
{
    CHECK(ready, "ConfigManager is not initialized");
    load();
}

void ConfigManager::applyDefaults()
{
    if (data.paths.assets.empty())
    {
        data.paths.assets = ".";
    }
    if (data.paths.meshDescs.empty())
    {
        data.paths.meshDescs = "descs/mesh";
    }
    if (data.paths.materialDescs.empty())
    {
        data.paths.materialDescs = "descs/material";
    }
    if (data.paths.textureDescs.empty())
    {
        data.paths.textureDescs = "descs/texture";
    }
    if (data.paths.shaderDescs.empty())
    {
        data.paths.shaderDescs = "descs/shader";
    }
    if (data.paths.sceneDescs.empty())
    {
        data.paths.sceneDescs = "descs/scene";
    }
    if (data.paths.geometry.empty())
    {
        data.paths.geometry = "geometry";
    }
    if (data.initialScene.empty())
    {
        data.initialScene = "scene:default";
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
    CHECK(!data.initialScene.empty(), "config '{}' missing initialScene", file.string());
    resolvePaths();
}

void ConfigManager::resolvePaths()
{
    data.paths.assets = resolveAgainst(configDir, std::move(data.paths.assets));
    CHECK(std::filesystem::is_directory(data.paths.assets),
        "asset root is not a directory: {}", data.paths.assets.string());

    data.paths.meshDescs = resolveAgainst(data.paths.assets, std::move(data.paths.meshDescs));
    data.paths.materialDescs =
        resolveAgainst(data.paths.assets, std::move(data.paths.materialDescs));
    data.paths.textureDescs =
        resolveAgainst(data.paths.assets, std::move(data.paths.textureDescs));
    data.paths.shaderDescs =
        resolveAgainst(data.paths.assets, std::move(data.paths.shaderDescs));
    data.paths.sceneDescs =
        resolveAgainst(data.paths.assets, std::move(data.paths.sceneDescs));
    data.paths.geometry = resolveAgainst(data.paths.assets, std::move(data.paths.geometry));
}
