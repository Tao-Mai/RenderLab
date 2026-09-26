#include "Engine.h"

#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
#include "Camera.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "editor/Editor.h"
#include "core/Logger.h"
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
    ctx.assetDescManager = new AssetDescManager();
    ctx.assetDataManager = new AssetDataManager();
    ctx.scene = new SceneDesc();
    ctx.camera = new Camera();
    ctx.renderer = new Renderer();
    ctx.editor = new Editor();

    ctx.config->init();
    ctx.assetDataManager->init();
    ctx.window->init();
    ctx.assetDescManager->init();
    inputMethod.activateEnglish();
    *ctx.scene = ctx.assetDescManager->desc<SceneDesc>(ctx.config->initialScene());
    ctx.camera->configure(ctx.scene->camera);
    ctx.renderer->init();
    ctx.renderer->loadScene(*ctx.scene);
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
        if (ctx.window->keyPressed(GLFW_KEY_ESCAPE))
        {
            ctx.window->requestClose();
            break;
        }

        const auto [framebufferWidth, framebufferHeight] = ctx.window->framebufferSize();
        if (framebufferWidth == 0 || framebufferHeight == 0)
        {
            ctx.window->waitEvents();
            previousTime = std::chrono::steady_clock::now();
            continue;
        }

        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTime =
            std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;

        ctx.camera->update(
            *ctx.window,
            deltaTime,
            ctx.camera->isNavigationActive() || !ctx.editor->wantsInput());

        const auto extent = ctx.renderer->swapchainHandle().extent();
        const EditorFrameInput editorInput = ctx.editor->buildFrame(
            *ctx.camera, deltaTime, extent.width, extent.height);
        const EditorFrameResult editorResult =
            ctx.renderer->render(*ctx.camera, editorInput);
        ctx.editor->applyPickResult(editorResult, ctx.renderer->gpuScene());
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
    if (ctx.renderer != nullptr)
    {
        ctx.renderer->shutdown();
        delete ctx.renderer;
        ctx.renderer = nullptr;
    }
    if (ctx.camera != nullptr)
    {
        delete ctx.camera;
        ctx.camera = nullptr;
    }
    if (ctx.scene != nullptr)
    {
        delete ctx.scene;
        ctx.scene = nullptr;
    }
    if (ctx.assetDescManager != nullptr)
    {
        ctx.assetDescManager->shutdown();
        delete ctx.assetDescManager;
        ctx.assetDescManager = nullptr;
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
