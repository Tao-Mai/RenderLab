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

namespace
{
template <class T>
void shutdownSubsystem(std::unique_ptr<T>& subsystem, T*& slot)
{
    DCHECK(subsystem);
    DCHECK(slot == subsystem.get());

    subsystem->shutdown();
    slot = nullptr;
    subsystem.reset();
}
}

Engine::Engine() = default;

Engine::~Engine()
{
    DCHECK(!inited);
    DCHECK(!config && !window && !inputManager && !assetManager &&
        !assetDataManager && !sceneManager && !renderer && !editor);
}

void Engine::init()
{
    DCHECK(!inited);

    Context& ctx = context();
    DCHECK(!ctx.config && !ctx.window && !ctx.inputManager && !ctx.assetManager &&
        !ctx.assetDataManager && !ctx.sceneManager && !ctx.renderer && !ctx.editor);

    config = std::make_unique<ConfigManager>();
    window = std::make_unique<Window>();
    inputManager = std::make_unique<InputManager>();
    assetManager = std::make_unique<AssetManager>();
    assetDataManager = std::make_unique<AssetDataManager>();
    sceneManager = std::make_unique<SceneManager>();
    renderer = std::make_unique<Renderer>();
    editor = std::make_unique<Editor>();

    ctx = {
        .config = config.get(),
        .window = window.get(),
        .inputManager = inputManager.get(),
        .assetManager = assetManager.get(),
        .assetDataManager = assetDataManager.get(),
        .sceneManager = sceneManager.get(),
        .renderer = renderer.get(),
        .editor = editor.get(),
    };

    ctx.config->init();
    ctx.assetDataManager->init();
    ctx.window->init();
    ctx.inputManager->init();
    ctx.sceneManager->init();
    ctx.assetManager->init();
    inputMethod.activateEnglish();
    ctx.renderer->init();
    ctx.editor->init();
    DEBUG_EXEC(inited = true);
}

void Engine::run()
{
    DCHECK(inited);

    sceneManager->loadDefaultScene();
    renderer->loadScene();

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
}

void Engine::shutdown() noexcept
{
    DCHECK(inited);
    Context& ctx = context();

    renderer->waitIdle();
    shutdownSubsystem(editor, ctx.editor);
    shutdownSubsystem(inputManager, ctx.inputManager);
    shutdownSubsystem(renderer, ctx.renderer);
    shutdownSubsystem(sceneManager, ctx.sceneManager);
    shutdownSubsystem(assetManager, ctx.assetManager);
    shutdownSubsystem(assetDataManager, ctx.assetDataManager);

    // Win8+: LoadKeyboardLayout+KLF_ACTIVATE only updates the system language while this
    // process still owns the focused window. Restore before destroying it.
    inputMethod.restore();

    shutdownSubsystem(window, ctx.window);
    shutdownSubsystem(config, ctx.config);

    DEBUG_EXEC(inited = false);
}
