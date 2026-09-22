#pragma once

#include "asset/asset_desc.h"
#include "asset/asset_manager.h"
#include "camera.h"
#include "core/config_manager.h"
#include "editor/editor.h"
#include "platform/input_method.h"
#include "render/renderer.h"
#include "scene/scene_manager.h"
#include "window.h"

class Engine
{
public:
    Engine();
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void init();
    void run();

private:
    bool initialized = false;

    ConfigManager config;
    Window window;
    InputMethod inputMethod;
    AssetManager assets;
    SceneManager scenes;
    SceneDesc scene;
    Camera camera;
    Renderer renderer;
    Editor editor;

    void mainLoop();
    void shutdown() noexcept;
};
