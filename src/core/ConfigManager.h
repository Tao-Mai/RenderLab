#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/AssetID.h"
#include "core/Debug.h"
#include "core/Input.h"
#include "core/Settings.h"
#include "render/RendererConfig.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct SceneAsset;
enum class SceneRenderMode;

inline constexpr char kRenderGraphOptionsPrefix[] = "renderGraphs:";

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

struct RL_RECORD AppConfig
{
    RL_FIELD("paths") AppPaths            paths;
    RL_FIELD("initialScene") AssetID<SceneAsset> initialScene;
    RL_FIELD("renderGraphs") std::map<RenderPath, std::map<std::string, std::filesystem::path, std::less<>>> renderGraphs;
};

struct RL_RECORD RendererSettings
{
    RL_FIELD("realtimeGraph") std::string realtimeGraph;
    RL_FIELD("offlineGraph") std::string offlineGraph;
    RL_FIELD("options") nlohmann::json options;
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
    [[nodiscard]] const auto& renderGraphs() const { return data.renderGraphs; }
    [[nodiscard]] const std::string&         renderGraphName() const noexcept;
    [[nodiscard]] std::filesystem::path      renderGraphScript() const;
    [[nodiscard]] const Settings&           getSettings() const { return settings; }
    [[nodiscard]] Settings&                 getSettings() { return settings; }
    [[nodiscard]] nlohmann::json            getOptions() const;
    void                                     applyGraphParameters(const nlohmann::json& parameters);
    void                                     setEditorSize(int width, int height);
    void                                     setObjectPicking(bool enabled);
    void                                     setInitialScene(AssetID<SceneAsset> id);
    void                                     setRenderGraph(std::string_view name);
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
    Settings              settings;
    SceneRenderMode       sceneRenderMode;
    std::string           realtimeGraph;
    std::string           offlineGraph;

    void load();
    void loadCommands();
    void loadEditor();
    void loadRenderer();
    void resolvePaths();
    [[nodiscard]] RenderPath findRenderPath(std::string_view graphName) const;
};

#include RL_METADATA_HEADER("metadata/src/core/ConfigManager.generated.h")
