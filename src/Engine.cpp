#include "Engine.h"

#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Plugin.h"
#include "core/Window.h"
#include "core/time/Timer.h"
#include "editor/Editor.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"

#include <tuple>
#include <utility>

namespace
{
template <class T>
void shutdownSubsystem(std::unique_ptr<T>& subsystem, T*& slot)
{

    subsystem->shutdown();
    slot = nullptr;
    subsystem.reset();
}
}  // namespace

Engine::Engine() = default;

Engine::~Engine() = default;

void Engine::init()
{

    Context& ctx = context();

    config           = std::make_unique<ConfigManager>();
    window           = std::make_unique<Window>();
    inputManager     = std::make_unique<InputManager>();
    assetManager     = std::make_unique<AssetManager>();
    assetDataManager = std::make_unique<AssetDataManager>();
    sceneManager     = std::make_unique<SceneManager>();
    renderer         = std::make_unique<Renderer>();
    editor           = std::make_unique<Editor>();

    ctx = {
        .config           = config.get(),
        .window           = window.get(),
        .inputManager     = inputManager.get(),
        .assetManager     = assetManager.get(),
        .assetDataManager = assetDataManager.get(),
        .sceneManager     = sceneManager.get(),
        .renderer         = renderer.get(),
        .editor           = editor.get(),
    };

    ctx.config->init();
    ctx.assetDataManager->init(ctx.config->paths().assets);
    ctx.window->init();
    ctx.inputManager->init();
    ctx.sceneManager->init();
    ctx.assetManager->init(ctx.config->paths().assets);
    ctx.sceneManager->loadDefaultScene();
    PluginManager::instance().loadAllPlugins();
    inputMethod.activateEnglish();
    ctx.renderer->init();
    ctx.editor->init();
}

void Engine::run() { mainLoop(); }

void Engine::mainLoop()
{
    Context& ctx = context();
    Timer    deltaTimer;
    bool     swapchainNeedsRebuild = false;
    while (!ctx.window->shouldClose())
    {
        ctx.window->pollEvents();
        ctx.inputManager->tick();
        if (ctx.inputManager->get(Command::Quit))
        {
            ctx.window->requestClose();
            break;
        }

        auto [framebufferWidth, framebufferHeight] = ctx.window->framebufferSize();
        if (framebufferWidth > 0 && framebufferHeight > 0 && ctx.editor->resizeWindowForLayout())
            std::tie(framebufferWidth, framebufferHeight) = ctx.window->framebufferSize();
        if (framebufferWidth == 0 || framebufferHeight == 0)
        {
            ctx.window->waitEvents();
            ctx.inputManager->tick();
            if (ctx.inputManager->get(Command::Quit))
                ctx.window->requestClose();
            deltaTimer.reset();
            continue;
        }

        const float deltaTime = static_cast<float>(deltaTimer.restartMilliseconds() / 1000.0);

        const auto currentExtent =
            Extent2D{ctx.renderer->getSurface()->getConfig()->width, ctx.renderer->getSurface()->getConfig()->height};
        if (swapchainNeedsRebuild || currentExtent.width != static_cast<uint32_t>(framebufferWidth) ||
            currentExtent.height != static_cast<uint32_t>(framebufferHeight))
        {
            ctx.renderer->resizeSurface();
            ctx.editor->refreshUi();
            swapchainNeedsRebuild = false;
        }

        ctx.editor->applySceneActions();

        auto& camera = ctx.editor->camera();
        ctx.editor->tick(deltaTime);
        const auto sceneCamera = camera.config().sceneCamera();
        ctx.inputManager->setNavigation({
            .forward = sceneCamera.forward(),
            .right   = sceneCamera.right(),
            .up      = sceneCamera.up(),
            .active  = camera.isNavigationActive(),
        });
        ctx.sceneManager->tick(deltaTime);

        const auto  editorFrame = ctx.editor->buildFrame(camera);
        const auto& renderData =
            renderSystem.prepare(ctx.sceneManager->scene(), ctx.sceneManager->sceneGeneration(), editorFrame.viewport);
        const auto result = ctx.renderer->render(renderData, editorFrame.input, deltaTime);
        swapchainNeedsRebuild = result == RenderFrameResult::SwapchainNeedsRebuild;
    }
}

void Engine::shutdown() noexcept
{

    Context& ctx = context();

    if (const auto& sceneId = sceneManager->scene().id; !sceneId.empty())
        config->setInitialScene(sceneId);

    renderer->waitIdle();
    shutdownSubsystem(editor, ctx.editor);
    shutdownSubsystem(inputManager, ctx.inputManager);
    renderSystem.reset();
    shutdownSubsystem(renderer, ctx.renderer);
    shutdownSubsystem(sceneManager, ctx.sceneManager);
    shutdownSubsystem(assetManager, ctx.assetManager);
    shutdownSubsystem(assetDataManager, ctx.assetDataManager);

    // Win8+: LoadKeyboardLayout+KLF_ACTIVATE only updates the system language while this
    // process still owns the focused window. Restore before destroying it.
    inputMethod.restore();

    shutdownSubsystem(window, ctx.window);
    shutdownSubsystem(config, ctx.config);
}
