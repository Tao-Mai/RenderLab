#pragma once

#include "core/Context.h"
#include "platform/InputMethod.h"
#include "scene/system/RenderSystem.h"

#include <memory>

class Engine
{
public:
    Engine();
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void init();
    void run();
    void shutdown() noexcept;

private:
    InputMethod                       inputMethod;
    RenderSystem                      renderSystem;
    std::unique_ptr<ConfigManager>    config;
    std::unique_ptr<Window>           window;
    std::unique_ptr<InputManager>     inputManager;
    std::unique_ptr<AssetManager>     assetManager;
    std::unique_ptr<AssetDataManager> assetDataManager;
    std::unique_ptr<SceneManager>     sceneManager;
    std::unique_ptr<Renderer>         renderer;
    std::unique_ptr<Editor>           editor;

    void mainLoop();
};
