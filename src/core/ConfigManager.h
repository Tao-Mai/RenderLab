#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/AssetID.h"
#include "core/Debug.h"
#include "core/Input.h"
#include "render/RendererConfig.h"
#include "render/graph/PassOverrides.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SceneAsset;
enum class SceneRenderMode;

struct RL_RECORD AppPaths
{
    RL_FIELD("assets") std::filesystem::path assets;
    RL_FIELD("commands") std::filesystem::path commands;
    RL_FIELD("editor") std::filesystem::path editor;
    RL_FIELD("renderer") std::filesystem::path renderer;
};

struct RL_RECORD CommandConfig
{
    RL_FIELD("bindings") std::vector<CommandBinding> bindings;
};

struct RL_RECORD EditorConfig
{
    RL_FIELD("width") int         width;
    RL_FIELD("height") int         height;
    RL_FIELD("title") std::string title;
    RL_FIELD("objectPicking") bool        objectPicking = false;
};

struct RL_RECORD RenderGraphDefinition
{
    RL_FIELD("id") std::string id;
    RL_FIELD("label") std::string label;
    RL_FIELD("renderPath") RenderPath renderPath;
    RL_FIELD("script") std::filesystem::path script;

    [[nodiscard]] bool isOffline() const { return renderPath == RenderPath::OfflinePathTracing; }
    [[nodiscard]] bool requiresRayTracing() const { return renderPath == RenderPath::RayTracing || isOffline(); }
};

struct RL_RECORD AppConfig
{
    RL_FIELD("paths") AppPaths            paths;
    RL_FIELD("initialScene") AssetID<SceneAsset> initialScene;
    RL_FIELD("renderGraphs") std::vector<RenderGraphDefinition> renderGraphs;
};

struct RL_RECORD GraphOverrides
{
    RL_FIELD("parameters") nlohmann::json parameters = nlohmann::json::object();
    RL_FIELD("passes") PassOverrides passes;
};

struct RL_RECORD RendererSettings
{
    RL_FIELD("realtimeGraph") std::string realtimeGraph;
    RL_FIELD("offlineGraph") std::string offlineGraph;
    RL_FIELD("graphOverrides") std::unordered_map<std::string, GraphOverrides> graphOverrides;
};

class ConfigManager
{
public:
    ConfigManager();
    ~ConfigManager();

    void init();
    void shutdown() noexcept;

    [[nodiscard]] const AppPaths&            paths() const noexcept { return resolvedPaths; }
    [[nodiscard]] const CommandConfig&       commandConfig() const noexcept;
    [[nodiscard]] const AssetID<SceneAsset>& initialScene() const noexcept;
    [[nodiscard]] const RendererConfig&      rendererConfig() const noexcept;
    [[nodiscard]] const EditorConfig&        editorConfig() const noexcept;
    [[nodiscard]] std::span<const RenderGraphDefinition> renderGraphs() const { return data.renderGraphs; }
    [[nodiscard]] const RenderGraphDefinition& renderGraph() const noexcept;
    [[nodiscard]] std::filesystem::path      renderGraphScript() const;
    [[nodiscard]] const nlohmann::json&      graphParameterOverrides() { return graphOverrides().parameters; }
    [[nodiscard]] uint64_t                   graphParameterRevision() const { return parameterRevision; }
    [[nodiscard]] PassOverrides&            passOverrides();
    void                                     setGraphParameterOverrides(nlohmann::json overrides);
    void                                     applyGraphParameters(const nlohmann::json& parameters);
    void                                     setEditorSize(int width, int height);
    void                                     setObjectPicking(bool enabled);
    void                                     setInitialScene(AssetID<SceneAsset> id);
    void                                     setRenderGraph(std::string_view id);
    void                                     setSceneRenderMode(SceneRenderMode mode);

private:
    DEBUG_ONLY(bool inited = false;)
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig             data;
    AppPaths              resolvedPaths;
    CommandConfig         commands;
    EditorConfig          editor;
    RendererConfig        renderer;
    RendererSettings      rendererSettings;
    uint64_t              parameterRevision = 0;
    SceneRenderMode       sceneRenderMode;
    const RenderGraphDefinition* realtimeGraph = nullptr;
    const RenderGraphDefinition* offlineGraph = nullptr;

    void load();
    void loadCommands();
    void loadEditor();
    void loadRenderer();
    void resolvePaths();
    [[nodiscard]] const RenderGraphDefinition& findRenderGraph(std::string_view id) const;
    GraphOverrides& graphOverrides();
};

#include RL_METADATA_HEADER("metadata/src/core/ConfigManager.generated.h")
