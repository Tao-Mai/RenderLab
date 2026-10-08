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
    asset_json::save(file, data);
    asset_json::save(resolvedPaths.renderer, renderer);

    data          = {};
    resolvedPaths = {};
    commands      = {};
    editor        = {};
    renderer      = {};
    file.clear();
    configDir.clear();
    DEBUG_EXEC(inited = false);
}

const CommandConfig& ConfigManager::commandConfig() const noexcept { return commands; }

const SceneAsset::ID& ConfigManager::initialScene() const noexcept { return data.initialScene; }

const RendererConfig& ConfigManager::rendererConfig() const noexcept { return renderer; }

const EditorConfig& ConfigManager::editorConfig() const noexcept { return editor; }

void ConfigManager::setInitialScene(SceneAsset::ID id)
{
    DCHECK(inited);
    CHECK(!id.empty());

    data.initialScene = std::move(id);
}

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
    CHECK(feature.shadow.rectAreaShadowMapSize > 0);
    CHECK(std::isfinite(feature.shadow.bias) && feature.shadow.bias >= 0.0f);
    CHECK(std::isfinite(feature.shadow.sampleRadius) && feature.shadow.sampleRadius >= 0.0f);
    CHECK(feature.shadow.filter == SamplerAsset::Filter::Nearest ||
          feature.shadow.filter == SamplerAsset::Filter::Linear);

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
    commands = asset_json::load<CommandConfig>(resolvedPaths.commands);

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
    resolvedPaths.assets   = resolveAgainst(configDir, data.paths.assets);
    resolvedPaths.commands = resolveAgainst(configDir, data.paths.commands);
    resolvedPaths.editor   = resolveAgainst(configDir, data.paths.editor);
    resolvedPaths.renderer = resolveAgainst(configDir, data.paths.renderer);
    CHECK(
        std::filesystem::is_regular_file(resolvedPaths.commands),
        "commands config is not a file: {}",
        resolvedPaths.commands.string()
    );
    CHECK(
        std::filesystem::is_directory(resolvedPaths.assets),
        "asset root is not a directory: {}",
        resolvedPaths.assets.string()
    );
}

void ConfigManager::loadEditor()
{
    editor = asset_json::load<EditorConfig>(resolvedPaths.editor);

    CHECK(
        editor.width > 0 && editor.height > 0,
        "editor config '{}' requires positive window width and height",
        resolvedPaths.editor.string()
    );
}

void ConfigManager::loadRenderer()
{
    renderer = asset_json::load<RendererConfig>(resolvedPaths.renderer);

    CHECK(!renderer.environmentMap || !renderer.environmentMap->empty());
    CHECK(renderer.feature.shadow.pointShadowMapSize > 0);
    CHECK(renderer.feature.shadow.rectAreaShadowMapSize > 0);
    CHECK(std::isfinite(renderer.feature.shadow.bias) && renderer.feature.shadow.bias >= 0.0f);
    CHECK(std::isfinite(renderer.feature.shadow.sampleRadius) && renderer.feature.shadow.sampleRadius >= 0.0f);

    CHECK(
        !renderer.brdfLut.textureID.empty() && !renderer.brdfLut.samplerID.empty(),
        "renderer config '{}' requires brdfLut textureID and samplerID",
        resolvedPaths.renderer.string()
    );
    CHECK(
        std::isfinite(renderer.feature.ambientCoefficient) && renderer.feature.ambientCoefficient >= 0.0f,
        "renderer config '{}' requires a finite nonnegative ambientCoefficient",
        resolvedPaths.renderer.string()
    );
}
