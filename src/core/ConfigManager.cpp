#include "core/ConfigManager.h"

#include "asset/Serializer.h"
#include "core/Logger.h"

#include <fstream>
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
    file      = std::filesystem::absolute(RENDERLAB_CONFIG_FILE).lexically_normal();
    configDir = file.parent_path();
    load();
    loadCommands();
    inited = true;
}

void ConfigManager::shutdown() noexcept
{
    data     = {};
    commands = {};
    file.clear();
    configDir.clear();
    inited = false;
}

const CommandConfig& ConfigManager::commandConfig() const noexcept
{
    return commands;
}

const AppPaths& ConfigManager::paths() const noexcept
{
    return data.paths;
}

const SceneAsset::ID& ConfigManager::initialScene() const noexcept
{
    return data.initialScene;
}

const RendererConfig& ConfigManager::rendererConfig() const noexcept
{
    return data.renderer;
}

void ConfigManager::save() const
{
    AppConfig stored                 = data;
    stored.paths.assets              = storeRelative(data.paths.assets, configDir);
    stored.paths.commands            = storeRelative(data.paths.commands, configDir);
    const std::string     serialized = Serialize(stored).dump(2);
    std::filesystem::path temporary  = file;
    temporary                        += ".tmp";

    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        CHECK(output.is_open(), "failed to open config temporary file '{}'", temporary.string());
        output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        output.close();
        CHECK(output, "failed to write config temporary file '{}'", temporary.string());
    }

    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    CHECK(!error, "failed to replace config '{}': {}", file.string(), error.message());
}

void ConfigManager::load()
{
    CHECK(std::filesystem::is_regular_file(file), "config is not a file: {}", file.string());

    std::ifstream input{file, std::ios::binary};
    CHECK(input.is_open(), "failed to open config '{}'", file.string());

    const auto stored = json::parse(input, nullptr, false);
    CHECK(!stored.is_discarded(), "invalid config JSON '{}'", file.string());
    Deserialize(stored, data);

    CHECK(!data.paths.assets.empty(), "config requires a paths.assets directory");
    CHECK(!data.paths.commands.empty(), "config requires a paths.commands file path");
    CHECK(!data.initialScene.empty(),
          "config '{}' missing initialScene",
          file.string());
    CHECK(!data.renderer.brdfLut.textureID.empty() &&
          !data.renderer.brdfLut.samplerID.empty(),
          "config '{}' requires renderer.brdfLut textureID and samplerID",
          file.string());
    resolvePaths();
}

void ConfigManager::loadCommands()
{
    std::ifstream input{data.paths.commands, std::ios::binary};
    CHECK(input.is_open(), "failed to open commands config '{}'", data.paths.commands.string());

    const auto stored = json::parse(input, nullptr, false);
    CHECK(!stored.is_discarded(), "invalid commands JSON '{}'", data.paths.commands.string());
    Deserialize(stored, commands);

    for (const auto& binding : commands.bindings)
    {
        CHECK(binding.key != Key::Unknown,
              "Unknown is not a bindable key");
        CHECK(binding.action != Action::Hold || binding.modifiers == Modifier::None,
              "Hold bindings do not support Modifiers");
    }
}

void ConfigManager::resolvePaths()
{
    data.paths.assets   = resolveAgainst(configDir, std::move(data.paths.assets));
    data.paths.commands = resolveAgainst(configDir, std::move(data.paths.commands));
    CHECK(std::filesystem::is_regular_file(data.paths.commands),
          "commands config is not a file: {}",
          data.paths.commands.string());
    CHECK(std::filesystem::is_directory(data.paths.assets),
          "asset root is not a directory: {}",
          data.paths.assets.string());
}
