#include "Engine.h"

#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "editor/Editor.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"

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
}  // namespace

Engine::Engine() = default;

Engine::~Engine()
{
    DCHECK(!inited);
    DCHECK(
        !config && !window && !inputManager && !assetManager && !assetDataManager && !sceneManager && !renderer &&
        !editor
    );
}

void Engine::init()
{
    DCHECK(!inited);

    Context& ctx = context();
    DCHECK(
        !ctx.config && !ctx.window && !ctx.inputManager && !ctx.assetManager && !ctx.assetDataManager &&
        !ctx.sceneManager && !ctx.renderer && !ctx.editor && !ctx.profiler
    );

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
        .profiler         = &profiler,
    };

    ctx.config->init();
    ctx.assetDataManager->init(ctx.config->paths().assets);
    ctx.window->init();
    ctx.inputManager->init();
    ctx.sceneManager->init();
    ctx.assetManager->init(ctx.config->paths().assets);
    inputMethod.activateEnglish();
    ctx.renderer->init();
    ctx.editor->init();
    DEBUG_EXEC(inited = true);
}

void Engine::run()
{
    DCHECK(inited);

    sceneManager->loadDefaultScene();

    mainLoop();
}

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

        const auto [framebufferWidth, framebufferHeight] = ctx.window->framebufferSize();
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
        profiler.beginFrame();

        const auto currentExtent = ctx.renderer->swapchainHandle().extent();
        if (swapchainNeedsRebuild || currentExtent.width != static_cast<uint32_t>(framebufferWidth) ||
            currentExtent.height != static_cast<uint32_t>(framebufferHeight))
        {
            ctx.renderer->recreateSwapchain();
            ctx.editor->refreshUi();
            swapchainNeedsRebuild = false;
        }

        {
            CpuProfileScope timing{&profiler, "Scene actions"};
            ctx.editor->applySceneActions();
        }
        auto& camera = ctx.editor->camera();
        {
            CpuProfileScope timing{&profiler, "Editor tick"};
            ctx.editor->tick(deltaTime);
        }
        {
            CpuProfileScope timing{&profiler, "Scene tick"};
            ctx.sceneManager->tick(deltaTime);
        }

        const auto extent      = ctx.renderer->swapchainHandle().extent();
        const auto editorFrame = [&]
        {
            CpuProfileScope timing{&profiler, "Editor UI"};
            return ctx.editor->buildFrame(camera, deltaTime, extent.width, extent.height);
        }();

        const auto& renderData   = renderSystem.prepare(ctx.sceneManager->scene());
        const auto  editorResult = ctx.renderer->render(editorFrame.view, renderData, editorFrame.input);
        swapchainNeedsRebuild    = editorResult.swapchainNeedsRebuild;
        ctx.editor->applyPickResult(editorResult);
        profiler.endFrame(deltaTime * 1000.0);
    }
}

void Engine::shutdown() noexcept
{
    DCHECK(inited);
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
    ctx.profiler = nullptr;
    profiler.reset();

    DEBUG_EXEC(inited = false);
}
