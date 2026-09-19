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
        renderer.initialize(window);
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
    window.shutdown();
    initialized = false;
}

