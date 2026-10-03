#pragma once

#include "asset/Asset.h"
#include "core/Input.h"
#include "core/Debug.h"

#include <filesystem>
#include <vector>

struct AppPaths
{
    std::filesystem::path assets;
    std::filesystem::path commands;
};

struct RendererConfig
{
    TextureBinding brdfLut;
};

struct CommandConfig
{
    std::vector<CommandBinding> bindings;
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

private:
    DEBUG_ONLY(bool inited = false;)
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig             data;
    CommandConfig         commands;

    void load();
    void loadCommands();
    void save() const;
    void resolvePaths();
};
