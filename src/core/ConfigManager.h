#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "asset/SceneAsset.h"
#include "core/Debug.h"
#include "core/Input.h"
#include "render/RendererConfig.h"

#include <filesystem>
#include <string>
#include <vector>

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
};

struct AppConfig
{
    AppPaths       paths;
    SceneAsset::ID initialScene;
};

class ConfigManager
{
public:
    ConfigManager() = default;
    ~ConfigManager();

    void init();
    void shutdown() noexcept;

    [[nodiscard]] const AppPaths&       paths() const noexcept { return resolvedPaths; }
    [[nodiscard]] const CommandConfig&  commandConfig() const noexcept;
    [[nodiscard]] const SceneAsset::ID& initialScene() const noexcept;
    [[nodiscard]] const RendererConfig& rendererConfig() const noexcept;
    [[nodiscard]] const EditorConfig&   editorConfig() const noexcept;
    void                                setInitialScene(SceneAsset::ID id);
    void                                setRenderPath(RenderPath path);
    void                                setRenderFeature(const RenderFeature& feature);
    void                                setEnvironmentMap(std::optional<EnvironmentMapAsset::ID> id);

private:
    DEBUG_ONLY(bool inited = false;)
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig             data;
    AppPaths              resolvedPaths;
    CommandConfig         commands;
    EditorConfig          editor;
    RendererConfig        renderer;

    void load();
    void loadCommands();
    void loadEditor();
    void loadRenderer();
    void resolvePaths();
};
