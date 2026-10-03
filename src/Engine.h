#pragma once

#include "platform/InputMethod.h"
#include "core/Context.h"
#include "core/Debug.h"

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
    DEBUG_ONLY(bool inited = false;)
    InputMethod inputMethod;
    std::unique_ptr<ConfigManager> config;
    std::unique_ptr<Window> window;
    std::unique_ptr<InputManager> inputManager;
    std::unique_ptr<AssetManager> assetManager;
    std::unique_ptr<AssetDataManager> assetDataManager;
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Editor> editor;

    void mainLoop();
};
