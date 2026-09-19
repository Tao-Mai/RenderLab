#include "engine.h"

#include <chrono>
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
        inputMethod.activateEnglish();
        scene = Scene::load(assets.path("scenes/default.scene.json"));
        camera.configure(scene.camera);
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
    auto previousTime = std::chrono::steady_clock::now();
    while (!window.shouldClose())
    {
        window.pollEvents();
        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(
            currentTime - previousTime).count();
        previousTime = currentTime;

        camera.update(window, deltaTime);
        renderer.render(camera);
    }

    renderer.waitIdle();
}

void Engine::shutdown() noexcept
{
    renderer.shutdown();
    assets.clear();
    inputMethod.restore();
    window.shutdown();
    initialized = false;
}
