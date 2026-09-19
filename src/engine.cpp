#include "engine.h"

#include <stdexcept>

Engine::~Engine()
{
    shutdown();
}

void Engine::initialize()
{
    if (initialized)
    {
        return;
    }

    try
    {
        window.initialize(800, 600, "RenderLab");
        scene = Scene::load(assets.path("scenes/default.scene.json"));
        renderer.initialize(window);
        renderer.loadScene(scene, assets);
        initialized = true;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

void Engine::run()
{
    if (!initialized)
    {
        throw std::logic_error("Engine must be initialized before run()");
    }

    mainLoop();
}

void Engine::mainLoop()
{
    while (!window.shouldClose())
    {
        window.pollEvents();
        renderer.render();
    }

    renderer.waitIdle();
}

void Engine::shutdown() noexcept
{
    renderer.shutdown();
    assets.clear();
    window.shutdown();
    initialized = false;
}
