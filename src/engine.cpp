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
        window.initialize(1440, 900, "RenderLab");
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
        if (window.keyPressed(GLFW_KEY_ESCAPE))
        {
            window.requestClose();
            break;
        }
        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(
            currentTime - previousTime).count();
        previousTime = currentTime;

        camera.update(
            window,
            deltaTime,
            camera.isNavigationActive() || !renderer.editorWantsInput());
        renderer.render(camera, deltaTime);
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
