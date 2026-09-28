#pragma once

#include "asset/AssetDesc.h"

#include <filesystem>

struct AppPaths
{
    std::filesystem::path assets;
};

struct RendererConfig
{
    Texture::ID brdfLut;
};

struct AppConfig
{
    AppPaths paths;
    Scene::ID initialScene;
    RendererConfig renderer;
};

class ConfigManager
{
public:
    ConfigManager() = default;

    void init();
    void shutdown() noexcept;

    [[nodiscard]] const AppPaths& paths() const noexcept;
    [[nodiscard]] const Scene::ID& initialScene() const noexcept;
    [[nodiscard]] const RendererConfig& rendererConfig() const noexcept;

private:
    bool inited = false;
    std::filesystem::path file;
    std::filesystem::path configDir;
    AppConfig data;

    void load();
    void save() const;
    void resolvePaths();
    void applyDefaults();
};
