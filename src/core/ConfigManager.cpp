#include "core/ConfigManager.h"

#include "asset/SceneAsset.h"
#include "core/Logger.h"
#include "core/Serializer.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
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

ConfigManager::ConfigManager() : sceneRenderMode(SceneRenderMode::Realtime) {}

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
    asset_json::save(resolvedPaths.renderer, rendererSettings);
    asset_json::save(resolvedPaths.editor, editor);

    data               = {};
    resolvedPaths      = {};
    commands           = {};
    editor             = {};
    renderer           = {};
    rendererSettings   = {};
    parameterRevision  = 0;
    sceneRenderMode    = SceneRenderMode::Realtime;
    realtimeGraph      = nullptr;
    offlineGraph       = nullptr;
    file.clear();
    configDir.clear();
    DEBUG_EXEC(inited = false);
}

const CommandConfig& ConfigManager::commandConfig() const noexcept { return commands; }

const AssetID<SceneAsset>& ConfigManager::initialScene() const noexcept { return data.initialScene; }

const RendererConfig& ConfigManager::rendererConfig() const noexcept { return renderer; }

const EditorConfig& ConfigManager::editorConfig() const noexcept { return editor; }

const RenderGraphDefinition& ConfigManager::renderGraph() const noexcept
{
    return *(sceneRenderMode == SceneRenderMode::Offline ? offlineGraph : realtimeGraph);
}

std::filesystem::path ConfigManager::renderGraphScript() const
{
    return resolveAgainst(configDir, renderGraph().script);
}

const RenderGraphDefinition& ConfigManager::findRenderGraph(std::string_view id) const
{
    const auto found = std::ranges::find(data.renderGraphs, id, &RenderGraphDefinition::id);
    CHECK(found != data.renderGraphs.end(), "unknown render graph '{}'", id);
    return *found;
}

PassOverrides& ConfigManager::passOverrides()
{
    return graphOverrides().passes;
}

GraphOverrides& ConfigManager::graphOverrides()
{
    return rendererSettings.graphOverrides[renderGraph().id];
}

void ConfigManager::setGraphParameterOverrides(nlohmann::json overrides)
{
    DCHECK(inited);
    DCHECK(overrides.is_object());
    auto& current = graphOverrides().parameters;
    if (current != overrides)
    {
        current = std::move(overrides);
        ++parameterRevision;
    }
}

void ConfigManager::applyGraphParameters(const nlohmann::json& parameters)
{
    CHECK(parameters.is_object(), "graph parameters must be an object");
    RenderFeature feature;
    metadata::visitFields(feature, [&](const auto& field, auto& value)
    {
        if (const auto found = parameters.find(field.name); found != parameters.end())
            Deserialize(*found, value);
    });
    CHECK(std::isfinite(feature.ambientCoefficient) && feature.ambientCoefficient >= 0.0f);
    renderer.feature = feature;
}

void ConfigManager::setEditorSize(int width, int height)
{
    DCHECK(inited);
    CHECK(width > 0 && height > 0);

    editor.width = width;
    editor.height = height;
}

void ConfigManager::setObjectPicking(bool enabled)
{
    DCHECK(inited);
    editor.objectPicking = enabled;
}

void ConfigManager::setInitialScene(AssetID<SceneAsset> id)
{
    DCHECK(inited);
    CHECK(!id.empty());

    data.initialScene = std::move(id);
}

void ConfigManager::setRenderGraph(std::string_view id)
{
    DCHECK(inited);

    const auto& graph = findRenderGraph(id);
    const bool offline = sceneRenderMode == SceneRenderMode::Offline;
    CHECK(graph.isOffline() == offline, "render graph '{}' does not match the scene render mode", id);
    auto& selection = offline ? rendererSettings.offlineGraph : rendererSettings.realtimeGraph;
    auto& active = offline ? offlineGraph : realtimeGraph;
    selection = graph.id;
    active = &graph;
    renderer.renderPath = graph.renderPath;
}

void ConfigManager::setSceneRenderMode(SceneRenderMode mode)
{
    DCHECK(inited);
    sceneRenderMode     = mode;
    renderer.renderPath = renderGraph().renderPath;
}

void ConfigManager::load()
{
    data = asset_json::load<AppConfig>(file);

    CHECK(!data.paths.assets.empty(), "config requires a paths.assets directory");
    CHECK(!data.paths.commands.empty(), "config requires a paths.commands file path");
    CHECK(!data.paths.editor.empty(), "config requires a paths.editor file path");
    CHECK(!data.paths.renderer.empty());
    CHECK(!data.initialScene.empty(), "config '{}' missing initialScene", file.string());

    std::unordered_set<std::string_view> graphIds;
    for (const auto& graph : data.renderGraphs)
    {
        CHECK(!graph.id.empty() && !graph.label.empty() && !graph.script.empty(), "incomplete render graph definition");
        const bool added = graphIds.insert(graph.id).second;
        CHECK(added, "duplicate render graph '{}'", graph.id);
        const auto script = resolveAgainst(configDir, graph.script);
        CHECK(std::filesystem::is_regular_file(script), "graph '{}' script is not a file: {}", graph.id, script.string());
    }
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
    resolvedPaths.assets      = resolveAgainst(configDir, data.paths.assets);
    resolvedPaths.commands    = resolveAgainst(configDir, data.paths.commands);
    resolvedPaths.editor      = resolveAgainst(configDir, data.paths.editor);
    resolvedPaths.renderer    = resolveAgainst(configDir, data.paths.renderer);
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
    rendererSettings = asset_json::load<RendererSettings>(resolvedPaths.renderer);
    realtimeGraph = &findRenderGraph(rendererSettings.realtimeGraph);
    offlineGraph = &findRenderGraph(rendererSettings.offlineGraph);
    CHECK(!realtimeGraph->isOffline(), "'{}' cannot be selected as the realtime graph", realtimeGraph->id);
    CHECK(offlineGraph->isOffline(), "'{}' cannot be selected as the offline graph", offlineGraph->id);
    for (const auto& [id, overrides] : rendererSettings.graphOverrides)
    {
        (void)findRenderGraph(id);
        CHECK(overrides.parameters.is_object(), "graph '{}' parameter overrides must be an object", id);
    }
    renderer.renderPath = renderGraph().renderPath;
}
