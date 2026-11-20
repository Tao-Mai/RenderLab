#pragma once

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

struct AppPaths
{
    std::filesystem::path assets;
    std::filesystem::path commands;
    std::filesystem::path editor;
    std::filesystem::path renderer;
};

struct CommandConfig
{
    std::vector<CommandBinding> bindings;
};

struct EditorConfig
{
    int         width;
    int         height;
    std::string title;
    bool        objectPicking = false;
};

struct RenderGraphDefinition
{
    std::string id;
    std::string label;
    RenderPath renderPath;
    std::filesystem::path script;

    [[nodiscard]] bool isOffline() const { return renderPath == RenderPath::OfflinePathTracing; }
    [[nodiscard]] bool requiresRayTracing() const { return renderPath == RenderPath::RayTracing || isOffline(); }
};

struct AppConfig
{
    AppPaths            paths;
    AssetID<SceneAsset> initialScene;
    std::vector<RenderGraphDefinition> renderGraphs;
};

struct GraphOverrides
{
    nlohmann::json parameters = nlohmann::json::object();
    PassOverrides passes;
};

struct RendererSettings
{
    std::string realtimeGraph;
    std::string offlineGraph;
    std::unordered_map<std::string, GraphOverrides> graphOverrides;
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
