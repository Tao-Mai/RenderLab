#pragma once

#include "asset/asset_manager.h"
#include "camera.h"
#include "platform/input_method.h"
#include "render/renderer.h"
#include "scene/scene.h"
#include "window.h"

class Engine
{
  public:
    Engine() = default;
    ~Engine();

    Engine(const Engine &)            = delete;
    Engine &operator=(const Engine &) = delete;

    void initialize();
    void run();

  private:
    bool     initialized = false;
    InputMethod inputMethod;
    Window   window;
    AssetManager assets;
    Scene scene;
    Camera camera;
    Renderer renderer;

    void mainLoop();
    void shutdown() noexcept;
};
