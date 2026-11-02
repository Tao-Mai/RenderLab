#pragma once

#include "asset/AssetID.h"
#include "core/Debug.h"
#include "core/Input.h"
#include "render/RendererConfig.h"

#include <filesystem>
#include <string>
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
    bool        lightMarkersAndPicking = false;
};

struct AppConfig
{
    AppPaths            paths;
    AssetID<SceneAsset> initialScene;
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
    void                                     setEditorSize(int width, int height);
    void                                     setLightMarkersAndPicking(bool enabled);
    void                                     setInitialScene(AssetID<SceneAsset> id);
    void                                     setRenderPath(RenderPath path);
    void                                     setSceneRenderMode(SceneRenderMode mode);
    void                                     setRenderFeature(const RenderFeature& feature);

private:
    DEBUG_ONLY(bool inited = false;)
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig             data;
    AppPaths              resolvedPaths;
    CommandConfig         commands;
    EditorConfig          editor;
    RendererConfig        renderer;
    SceneRenderMode       sceneRenderMode;
    RenderPath            realtimeRenderPath = RenderPath::Forward;

    void load();
    void loadCommands();
    void loadEditor();
    void loadRenderer();
    void resolvePaths();
};
