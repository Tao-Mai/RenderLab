#include "core/ConfigManager.h"

#include "asset/BuiltinAssets.h"
#include "asset/JsonIo.h"
#include "core/Logger.h"

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
    if (!relative.empty())
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

const AppPaths& ConfigManager::paths() const noexcept
{
    return data.paths;
}

const Scene::ID& ConfigManager::initialScene() const noexcept
{
    return data.initialScene;
}

const RendererConfig& ConfigManager::rendererConfig() const noexcept
{
    return data.renderer;
}

void ConfigManager::save() const
{
    AppConfig stored = data;
    stored.paths.assets = storeRelative(data.paths.assets, configDir);
    asset_json::save(file, stored);
}

void ConfigManager::applyDefaults()
{
    if (data.paths.assets.empty())
    {
        data.paths.assets = "../assets";
    }
    if (data.initialScene.empty())
    {
        data.initialScene = "default";
    }
}

void ConfigManager::load()
{
    if (!std::filesystem::exists(file))
    {
        applyDefaults();
        data.renderer.brdfLut = TextureBinding{
            Texture::ID{"lut_ggx"}, BuiltinAssets::Sampler::linearClamp};
        resolvePaths();
        save();
        return;
    }

    data = asset_json::load<AppConfig>(file);
    applyDefaults();
    CHECK(!data.initialScene.empty(),
        "config '{}' missing initialScene", file.string());
    CHECK(!data.renderer.brdfLut.textureID.empty() &&
          !data.renderer.brdfLut.samplerID.empty(),
          "config '{}' requires renderer.brdfLut textureID and samplerID",
          file.string());
    resolvePaths();
}

void ConfigManager::resolvePaths()
{
    data.paths.assets = resolveAgainst(configDir, std::move(data.paths.assets));
    CHECK(std::filesystem::is_directory(data.paths.assets),
        "asset root is not a directory: {}", data.paths.assets.string());
}
