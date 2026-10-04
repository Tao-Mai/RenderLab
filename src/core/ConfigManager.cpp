#include "core/ConfigManager.h"

#include "core/Serializer.h"
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

ConfigManager::~ConfigManager()
{
    DCHECK(!inited);
}

void ConfigManager::init()
{
    DCHECK(!inited);

    file      = std::filesystem::absolute(RENDERLAB_CONFIG_FILE).lexically_normal();
    configDir = file.parent_path();
    load();
    loadCommands();
    loadEditor();
    DEBUG_EXEC(inited = true);
}

void ConfigManager::shutdown() noexcept
{
    data     = {};
    commands = {};
    editor   = {};
    file.clear();
    configDir.clear();
    DEBUG_EXEC(inited = false);
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

const EditorConfig& ConfigManager::editorConfig() const noexcept
{
    return editor;
}

void ConfigManager::setRenderPath(RenderPath path, bool persist)
{
    DCHECK(inited);
    data.renderer.renderPath = path;
    if (persist) save();
}

void ConfigManager::save() const
{
    AppConfig stored                 = data;
    stored.paths.assets              = storeRelative(data.paths.assets, configDir);
    stored.paths.commands            = storeRelative(data.paths.commands, configDir);
    stored.paths.editor              = storeRelative(data.paths.editor, configDir);
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
    data = asset_json::load<AppConfig>(file);

    CHECK(!data.paths.assets.empty(), "config requires a paths.assets directory");
    CHECK(!data.paths.commands.empty(), "config requires a paths.commands file path");
    CHECK(!data.paths.editor.empty(), "config requires a paths.editor file path");
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
    commands = asset_json::load<CommandConfig>(data.paths.commands);

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
    data.paths.editor   = resolveAgainst(configDir, std::move(data.paths.editor));
    CHECK(std::filesystem::is_regular_file(data.paths.commands),
          "commands config is not a file: {}",
          data.paths.commands.string());
    CHECK(std::filesystem::is_directory(data.paths.assets),
          "asset root is not a directory: {}",
          data.paths.assets.string());
}

void ConfigManager::loadEditor()
{
    editor = asset_json::load<EditorConfig>(data.paths.editor);

    CHECK(editor.width > 0 && editor.height > 0,
        "editor config '{}' requires positive window width and height", data.paths.editor.string());
}
