#include "core/ConfigManager.h"

#include "core/Logger.h"
#include "core/Serializer.h"

#include <cmath>
#include <utility>

namespace
{
std::filesystem::path resolveAgainst(const std::filesystem::path& baseDir, std::filesystem::path value)
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
}  // namespace

ConfigManager::~ConfigManager() { DCHECK(!inited); }

void ConfigManager::init()
{
    DCHECK(!inited);

    file      = std::filesystem::absolute(RENDERLAB_CONFIG_FILE).lexically_normal();
    configDir = file.parent_path();
    load();
    loadRenderer();
    loadCommands();
    loadEditor();
    DEBUG_EXEC(inited = true);
}

void ConfigManager::shutdown() noexcept
{
    DCHECK(inited);
    asset_json::save(data.paths.renderer, renderer);

    data     = {};
    commands = {};
    editor   = {};
    renderer = {};
    file.clear();
    configDir.clear();
    DEBUG_EXEC(inited = false);
}

const CommandConfig& ConfigManager::commandConfig() const noexcept { return commands; }

const AppPaths& ConfigManager::paths() const noexcept { return data.paths; }

const SceneAsset::ID& ConfigManager::initialScene() const noexcept { return data.initialScene; }

const RendererConfig& ConfigManager::rendererConfig() const noexcept { return renderer; }

const EditorConfig& ConfigManager::editorConfig() const noexcept { return editor; }

void ConfigManager::setRenderPath(RenderPath path)
{
    DCHECK(inited);
    renderer.renderPath = path;
}

void ConfigManager::setRenderFeature(const RenderFeature& feature)
{
    DCHECK(inited);
    CHECK(std::isfinite(feature.ambientCoefficient) && feature.ambientCoefficient >= 0.0f);
    CHECK(feature.shadow.pointShadowMapSize > 0);

    renderer.feature = feature;
}

void ConfigManager::setEnvironmentMap(std::optional<EnvironmentMapAsset::ID> id)
{
    DCHECK(inited);
    CHECK(!id || !id->empty());

    renderer.environmentMap = std::move(id);
}

void ConfigManager::load()
{
    data = asset_json::load<AppConfig>(file);

    CHECK(!data.paths.assets.empty(), "config requires a paths.assets directory");
    CHECK(!data.paths.commands.empty(), "config requires a paths.commands file path");
    CHECK(!data.paths.editor.empty(), "config requires a paths.editor file path");
    CHECK(!data.paths.renderer.empty());
    CHECK(!data.initialScene.empty(), "config '{}' missing initialScene", file.string());
    resolvePaths();
}

void ConfigManager::loadCommands()
{
    commands = asset_json::load<CommandConfig>(data.paths.commands);

    for (const auto& binding : commands.bindings)
    {
        CHECK(binding.key != Key::Unknown, "Unknown is not a bindable key");
        CHECK(
            binding.action != Action::Hold || binding.modifiers == Modifier::None,
            "Hold bindings do not support Modifiers"
        );
    }
}

void ConfigManager::resolvePaths()
{
    data.paths.assets   = resolveAgainst(configDir, std::move(data.paths.assets));
    data.paths.commands = resolveAgainst(configDir, std::move(data.paths.commands));
    data.paths.editor   = resolveAgainst(configDir, std::move(data.paths.editor));
    data.paths.renderer = resolveAgainst(configDir, std::move(data.paths.renderer));
    CHECK(
        std::filesystem::is_regular_file(data.paths.commands),
        "commands config is not a file: {}",
        data.paths.commands.string()
    );
    CHECK(
        std::filesystem::is_directory(data.paths.assets),
        "asset root is not a directory: {}",
        data.paths.assets.string()
    );
}

void ConfigManager::loadEditor()
{
    editor = asset_json::load<EditorConfig>(data.paths.editor);

    CHECK(
        editor.width > 0 && editor.height > 0,
        "editor config '{}' requires positive window width and height",
        data.paths.editor.string()
    );
}

void ConfigManager::loadRenderer()
{
    renderer = asset_json::load<RendererConfig>(data.paths.renderer);

    CHECK(!renderer.environmentMap || !renderer.environmentMap->empty());
    CHECK(renderer.feature.shadow.pointShadowMapSize > 0);

    CHECK(
        !renderer.brdfLut.textureID.empty() && !renderer.brdfLut.samplerID.empty(),
        "renderer config '{}' requires brdfLut textureID and samplerID",
        data.paths.renderer.string()
    );
    CHECK(
        std::isfinite(renderer.feature.ambientCoefficient) && renderer.feature.ambientCoefficient >= 0.0f,
        "renderer config '{}' requires a finite nonnegative ambientCoefficient",
        data.paths.renderer.string()
    );
}
