#include "Engine.h"

#include "asset/AssetManager.h"
#include "asset/AssetDataManager.h"
#include "scene/SceneManager.h"
#include "scene/component/CameraComponent.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "editor/Editor.h"
#include "core/Logger.h"
#include "core/InputManager.h"
#include "render/Renderer.h"
#include "core/Window.h"

#include <chrono>

Engine::~Engine()
{
    shutdown();
}

void Engine::init()
{
    if (inited)
    {
        return;
    }

    Context& ctx = context();
    ctx.config = new ConfigManager();
    ctx.window = new Window();
    ctx.inputManager = new InputManager();
    ctx.assetManager = new AssetManager();
    ctx.assetDataManager = new AssetDataManager();
    ctx.sceneManager = new SceneManager();
    ctx.renderer = new Renderer();
    ctx.editor = new Editor();

    ctx.config->init();
    ctx.assetDataManager->init();
    ctx.window->init();
    ctx.inputManager->init();
    RegisterSceneTypes();
    ctx.assetManager->init();
    inputMethod.activateEnglish();
    ctx.sceneManager->load(ctx.config->initialScene());
    ctx.renderer->init();
    ctx.renderer->loadScene();
    ctx.editor->init();
    inited = true;
}

void Engine::run()
{
    CHECK(inited, "Engine must be initialized before run()");
    mainLoop();
}

void Engine::mainLoop()
{
    Context& ctx = context();
    auto previousTime = std::chrono::steady_clock::now();
    while (!ctx.window->shouldClose())
    {
        ctx.window->pollEvents();
        ctx.inputManager->tick();
        if (ctx.inputManager->get(Command::Quit))
        {
            ctx.window->requestClose();
            break;
        }

        const auto [framebufferWidth, framebufferHeight] = ctx.window->framebufferSize();
        if (framebufferWidth == 0 || framebufferHeight == 0)
        {
            ctx.window->waitEvents();
            ctx.inputManager->tick();
            if (ctx.inputManager->get(Command::Quit)) ctx.window->requestClose();
            previousTime = std::chrono::steady_clock::now();
            continue;
        }

        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTime =
            std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;

        auto& camera = ctx.sceneManager->editorCamera().camera();
        camera.setInputEnabled(camera.isNavigationActive() || !ctx.editor->wantsInput());
        ctx.sceneManager->tick(deltaTime);

        const auto extent = ctx.renderer->swapchainHandle().extent();
        const EditorFrameInput editorInput = ctx.editor->buildFrame(
            camera, deltaTime, extent.width, extent.height);
        const EditorFrameResult editorResult =
            ctx.renderer->render(camera, editorInput);
        ctx.editor->applyPickResult(editorResult);
    }

    ctx.renderer->waitIdle();
}

void Engine::shutdown() noexcept
{
    Context& ctx = context();

    if (ctx.editor != nullptr)
    {
        ctx.editor->shutdown();
        delete ctx.editor;
        ctx.editor = nullptr;
    }
    if (ctx.inputManager != nullptr)
    {
        ctx.inputManager->shutdown();
        delete ctx.inputManager;
        ctx.inputManager = nullptr;
    }
    if (ctx.renderer != nullptr)
    {
        ctx.renderer->shutdown();
        delete ctx.renderer;
        ctx.renderer = nullptr;
    }
    if (ctx.sceneManager != nullptr)
    {
        ctx.sceneManager->reset();
        delete ctx.sceneManager;
        ctx.sceneManager = nullptr;
    }
    if (ctx.assetManager != nullptr)
    {
        ctx.assetManager->shutdown();
        delete ctx.assetManager;
        ctx.assetManager = nullptr;
    }
    if (ctx.assetDataManager != nullptr)
    {
        delete ctx.assetDataManager;
        ctx.assetDataManager = nullptr;
    }

    // Win8+: LoadKeyboardLayout+KLF_ACTIVATE only updates the system language while this
    // process still owns the focused window. Restore before destroying it.
    inputMethod.restore();

    if (ctx.window != nullptr)
    {
        ctx.window->shutdown();
        delete ctx.window;
        ctx.window = nullptr;
    }
    if (ctx.config != nullptr)
    {
        ctx.config->shutdown();
        delete ctx.config;
        ctx.config = nullptr;
    }

    inited = false;
}
