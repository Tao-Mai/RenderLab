#include "engine.h"

#include <chrono>

#include "logger.h"

Engine::Engine() :
    config(RENDERLAB_CONFIG_FILE),
    assets(config.assetRoot()),
    scenes(config.assetRoot())
{
}

Engine::~Engine()
{
    shutdown();
}

void Engine::init()
{
    if (initialized)
    {
        return;
    }

    try
    {
        window.init(1440, 900, "RenderLab");
        inputMethod.activateEnglish();
        scene = scenes.load(config.initialScene());
        camera.configure(scene.camera);
        renderer.init(window, assets);
        renderer.loadScene(scene);
        editor.init(
            window.nativeHandle(),
            renderer.vulkanContext(),
            renderer.swapchainHandle());
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
    CHECK(initialized, "Engine must be initialized before run()");
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
        const float deltaTime =
            std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;

        camera.update(
            window,
            deltaTime,
            camera.isNavigationActive() || !editor.wantsInput());

        const auto extent = renderer.swapchainHandle().extent();
        const EditorFrameInput editorInput =
            editor.buildFrame(camera, deltaTime, extent.width, extent.height);
        const EditorFrameResult editorResult = renderer.render(camera, editorInput);
        editor.applyPickResult(editorResult, renderer.gpuScene());
    }

    renderer.waitIdle();
}

void Engine::shutdown() noexcept
{
    editor.shutdown();
    renderer.shutdown();
    scenes.clear();
    assets.clear();
    inputMethod.restore();
    window.shutdown();
    initialized = false;
}
