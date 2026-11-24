#include "core/ConfigManager.h"

#include "asset/SceneAsset.h"
#include "core/Logger.h"
#include "core/Serializer.h"

#include <algorithm>
#include <cmath>
#include <ranges>
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

ConfigManager::~ConfigManager() {}

void ConfigManager::init()
{
    file      = std::filesystem::absolute(RENDERLAB_CONFIG_FILE).lexically_normal();
    configDir = file.parent_path();
    load();
    loadRenderer();
    loadCommands();
    loadEditor();
}

void ConfigManager::shutdown() noexcept
{

    asset_json::save(file, data);
    asset_json::save(
        resolvedPaths.renderer,
        RendererSettings{realtimeGraph, offlineGraph, nlohmann::json(settings.getOptions())}
    );
    asset_json::save(resolvedPaths.editor, editor);

    data          = {};
    resolvedPaths = {};
    commands      = {};
    editor        = {};
    renderer      = {};
    settings.clearOptions();
    sceneRenderMode = SceneRenderMode::Realtime;
    realtimeGraph.clear();
    offlineGraph.clear();
    file.clear();
    configDir.clear();
}

const CommandConfig& ConfigManager::commandConfig() const noexcept { return commands; }

const AssetID<SceneAsset>& ConfigManager::initialScene() const noexcept { return data.initialScene; }

const RendererConfig& ConfigManager::rendererConfig() const noexcept { return renderer; }

const EditorConfig& ConfigManager::editorConfig() const noexcept { return editor; }

const std::string& ConfigManager::renderGraphName() const noexcept
{
    return sceneRenderMode == SceneRenderMode::Offline ? offlineGraph : realtimeGraph;
}

std::filesystem::path ConfigManager::renderGraphScript() const
{
    return resolveAgainst(configDir, data.renderGraphs.at(renderer.renderPath).at(renderGraphName()));
}

RenderPath ConfigManager::findRenderPath(std::string_view graphName) const
{
    const auto found = std::ranges::find_if(
        data.renderGraphs,
        [graphName](const auto& group) { return group.second.contains(graphName); }
    );
    CHECK(found != data.renderGraphs.end(), "unknown render graph '{}'", graphName);
    return found->first;
}

nlohmann::json ConfigManager::getOptions() const
{
    const auto     prefix  = kRenderGraphOptionsPrefix + settings::encodeName(renderGraphName()) + ':';
    const auto     options = nlohmann::json(settings.getOptions());
    nlohmann::json result{{"parameters", nlohmann::json::object()}, {"passes", nlohmann::json::object()}};
    for (const auto& [name, value] : options.items())
    {
        if (!name.starts_with(prefix) || value.is_null())
            continue;

        const auto key   = std::string_view{name}.substr(prefix.size());
        auto*      field = &result;
        for (const auto part : key | std::views::split(':'))
        {
            CHECK(field->is_null() || field->is_object(), "conflicting option '{}'", name);
            field = &(*field)[settings::decodeName(std::string_view{part})];
        }
        *field = value;
    }

    CHECK(
        result.at("parameters").is_object() && result.at("passes").is_object(),
        "invalid options for graph '{}'",
        renderGraphName()
    );
    return result;
}

void ConfigManager::applyGraphParameters(const nlohmann::json& parameters)
{
    CHECK(parameters.is_object(), "graph parameters must be an object");
    RenderFeature feature;
    metadata::visitFields(
        feature,
        [&](const auto& field, auto& value)
        {
            if (const auto found = parameters.find(field.name); found != parameters.end())
                Deserialize(*found, value);
        }
    );
    CHECK(std::isfinite(feature.ambientCoefficient) && feature.ambientCoefficient >= 0.0f);
    renderer.feature = feature;
}

void ConfigManager::setEditorSize(int width, int height)
{

    CHECK(width > 0 && height > 0);

    editor.width  = width;
    editor.height = height;
}

void ConfigManager::setObjectPicking(bool enabled) { editor.objectPicking = enabled; }

void ConfigManager::setInitialScene(AssetID<SceneAsset> id)
{

    CHECK(!id.empty());

    data.initialScene = std::move(id);
}

void ConfigManager::setRenderGraph(std::string_view name)
{


    const auto path    = findRenderPath(name);
    const bool offline = sceneRenderMode == SceneRenderMode::Offline;
    CHECK(
        (path == RenderPath::OfflinePathTracing) == offline,
        "render graph '{}' does not match the scene render mode",
        name
    );
    auto& active        = offline ? offlineGraph : realtimeGraph;
    active              = name;
    renderer.renderPath = path;
}

void ConfigManager::setSceneRenderMode(SceneRenderMode mode)
{

    sceneRenderMode     = mode;
    renderer.renderPath = findRenderPath(renderGraphName());
}

void ConfigManager::load()
{
    data = asset_json::load<AppConfig>(file);

    CHECK(!data.paths.assets.empty(), "config requires a paths.assets directory");
    CHECK(!data.paths.commands.empty(), "config requires a paths.commands file path");
    CHECK(!data.paths.editor.empty(), "config requires a paths.editor file path");
    CHECK(!data.paths.renderer.empty());
    CHECK(!data.initialScene.empty(), "config '{}' missing initialScene", file.string());

    std::unordered_set<std::string_view> graphNames;
    for (const auto& [path, scripts] : data.renderGraphs)
    {
        for (const auto& [name, scriptPath] : scripts)
        {
            CHECK(!name.empty() && !scriptPath.empty(), "render graph requires a name and a script path");
            const bool added = graphNames.insert(name).second;
            CHECK(added, "duplicate render graph '{}'", name);
            const auto script = resolveAgainst(configDir, scriptPath);
            CHECK(
                std::filesystem::is_regular_file(script),
                "graph '{}' script is not a file: {}",
                name,
                script.string()
            );
        }
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
    const auto rendererSettings = asset_json::load<RendererSettings>(resolvedPaths.renderer);
    realtimeGraph               = rendererSettings.realtimeGraph;
    offlineGraph                = rendererSettings.offlineGraph;
    const auto realtimePath     = findRenderPath(realtimeGraph);
    CHECK(
        realtimePath != RenderPath::OfflinePathTracing,
        "'{}' cannot be selected as the realtime graph",
        realtimeGraph
    );
    CHECK(
        findRenderPath(offlineGraph) == RenderPath::OfflinePathTracing,
        "'{}' cannot be selected as the offline graph",
        offlineGraph
    );
    settings.addOptions(rendererSettings.options);
    const auto                 options = nlohmann::json(settings.getOptions());
    constexpr std::string_view kPrefix = kRenderGraphOptionsPrefix;
    for (const auto& [name, value] : options.items())
    {
        if (!name.starts_with(kPrefix))
            continue;

        const auto key = std::string_view{name}.substr(kPrefix.size());
        (void)findRenderPath(settings::decodeName(key.substr(0, key.find(':'))));
    }
    renderer.renderPath = realtimePath;
}
