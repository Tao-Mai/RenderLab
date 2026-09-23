#include "engine.h"

#include "asset/asset_manager.h"
#include "camera.h"
#include "core/config_manager.h"
#include "core/context.h"
#include "editor/editor.h"
#include "core/logger.h"
#include "render/renderer.h"
#include "core/window.h"

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
    ctx.assets = new AssetManager();
    ctx.scene = new SceneDesc();
    ctx.camera = new Camera();
    ctx.renderer = new Renderer();
    ctx.editor = new Editor();

    ctx.config->init();
    ctx.window->init();
    ctx.assets->init();
    inputMethod.activateEnglish();
    *ctx.scene = ctx.assets->desc<SceneDesc>(ctx.config->initialScene());
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
    if (ctx.assets != nullptr)
    {
        ctx.assets->shutdown();
        delete ctx.assets;
        ctx.assets = nullptr;
    }
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

    inputMethod.restore();
    inited = false;
}
