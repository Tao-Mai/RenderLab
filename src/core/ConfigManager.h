#pragma once

#include "asset/Asset.h"
#include "core/Input.h"
#include "core/Debug.h"

#include <filesystem>
#include <string>
#include <vector>

struct AppPaths
{
    std::filesystem::path assets;
    std::filesystem::path commands;
    std::filesystem::path editor;
};

enum class RenderPath { Rasterization, RayTracing };

struct RendererConfig
{
    RenderPath renderPath;
    TextureBinding brdfLut;
};

struct CommandConfig
{
    std::vector<CommandBinding> bindings;
};

struct EditorConfig
{
    int width;
    int height;
    std::string title;
};

struct AppConfig
{
    AppPaths       paths;
    SceneAsset::ID      initialScene;
    RendererConfig renderer;
};

class ConfigManager
{
public:
    ConfigManager() = default;
    ~ConfigManager();

    void init();
    void shutdown() noexcept;

    [[nodiscard]] const AppPaths&       paths() const noexcept;
    [[nodiscard]] const CommandConfig&  commandConfig() const noexcept;
    [[nodiscard]] const SceneAsset::ID&      initialScene() const noexcept;
    [[nodiscard]] const RendererConfig& rendererConfig() const noexcept;
    [[nodiscard]] const EditorConfig& editorConfig() const noexcept;
    void setRenderPath(RenderPath path, bool persist = true);

private:
    DEBUG_ONLY(bool inited = false;)
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig             data;
    CommandConfig         commands;
    EditorConfig          editor;

    void load();
    void loadCommands();
    void loadEditor();
    void save() const;
    void resolvePaths();
};
