#include "Engine.h"

#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
#include "ecs/SceneManager.h"
#include "ecs/system/CameraSystem.h"
#include "ecs/system/FreeFlyMoveSystem.h"
#include "ecs/system/CharacterMoveSystem.h"
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
    ctx.assetDescManager = new AssetDescManager();
    ctx.assetDataManager = new AssetDataManager();
    ctx.sceneManager = new SceneManager();
    ctx.cameraSystem = new ecs::CameraSystem();
    ctx.freeFlyMoveSystem = new ecs::FreeFlyMoveSystem();
    ctx.characterMoveSystem = new ecs::CharacterMoveSystem();
    ctx.renderer = new Renderer();
    ctx.editor = new Editor();

    ctx.config->init();
    ctx.assetDataManager->init();
    ctx.window->init();
    ctx.inputManager->init();
    ctx.assetDescManager->init();
    inputMethod.activateEnglish();
    ctx.sceneManager->load(ctx.config->initialScene());
    ctx.cameraSystem->reset();
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

        bool sceneEdited = ctx.cameraSystem->tick(
            ctx.cameraSystem->isNavigationActive() || !ctx.editor->wantsInput());
        sceneEdited |= ctx.freeFlyMoveSystem->tick(deltaTime);
        sceneEdited |= ctx.characterMoveSystem->tick(deltaTime);
        if (sceneEdited) ctx.editor->markDirty();

        const auto extent = ctx.renderer->swapchainHandle().extent();
        const EditorFrameInput editorInput = ctx.editor->buildFrame(
            *ctx.cameraSystem, deltaTime, extent.width, extent.height);
        const EditorFrameResult editorResult =
            ctx.renderer->render(*ctx.cameraSystem, editorInput);
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
    if (ctx.freeFlyMoveSystem != nullptr)
    {
        delete ctx.freeFlyMoveSystem;
        ctx.freeFlyMoveSystem = nullptr;
    }
    if (ctx.characterMoveSystem != nullptr)
    {
        delete ctx.characterMoveSystem;
        ctx.characterMoveSystem = nullptr;
    }
    if (ctx.cameraSystem != nullptr)
    {
        ctx.cameraSystem->reset();
        delete ctx.cameraSystem;
        ctx.cameraSystem = nullptr;
    }
    if (ctx.sceneManager != nullptr)
    {
        delete ctx.sceneManager;
        ctx.sceneManager = nullptr;
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
