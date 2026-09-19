#pragma once

#include "renderer.h"
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
    Window   window;
    Renderer renderer;

    void mainLoop();
    void shutdown() noexcept;
};
