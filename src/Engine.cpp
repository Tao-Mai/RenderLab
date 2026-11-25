#include "Engine.h"

#include "asset/AssetManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Json.h"
#include "core/utils/Timer.h"
#include "editor/Editor.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"

#include <GLFW/glfw3.h>
#include <tuple>

namespace
{
const std::filesystem::path kConfigFile{RENDERLAB_CONFIG_FILE};

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

void Engine::loadConfig()
{
    auto& config = context().config;
    config = asset_json::load<Config>(kConfigFile);

    CHECK(!config.assets.empty(), "config '{}' requires an asset directory", kConfigFile.string());
    CHECK(!config.initialScene.empty(), "config '{}' requires an initial scene", kConfigFile.string());
    CHECK(config.editor.width > 0 && config.editor.height > 0);
}

std::filesystem::path Engine::assetRoot()
{
    return (kConfigFile.parent_path() / context().config.assets).lexically_normal();
}

void Engine::init()
{
    Context& ctx = context();

    assetManager     = std::make_unique<AssetManager>();
    sceneManager     = std::make_unique<SceneManager>();
    renderer         = std::make_unique<Renderer>();
    editor           = std::make_unique<Editor>();

    ctx = {
        .assetManager     = assetManager.get(),
        .sceneManager     = sceneManager.get(),
        .renderer         = renderer.get(),
        .editor           = editor.get(),
    };

    loadConfig();
    const auto assets = assetRoot();

    initWindow();
    ctx.window = &window;
    ctx.assetManager->init(assets);
    ctx.sceneManager->loadDefaultScene();
    ctx.renderer->init();
    ctx.editor->init();
}

void Engine::initWindow()
{
    auto& settings = context().config.editor;
    window.init(settings.width, settings.height, settings.title.c_str());
    std::tie(settings.width, settings.height) = window.size();

    glfwSetWindowSizeCallback(window.nativeHandle(), [](GLFWwindow*, int newWidth, int newHeight)
    {
        if (newWidth > 0 && newHeight > 0)
        {
            auto& editorSettings = context().config.editor;
            editorSettings.width = newWidth;
            editorSettings.height = newHeight;
        }
    });
}

void Engine::run() { mainLoop(); }

void Engine::mainLoop()
{
    Context& ctx = context();
    Timer    deltaTimer;
    bool     swapchainNeedsRebuild = false;
    
    while (!window.shouldClose())
    {
        auto [framebufferWidth, framebufferHeight] = window.framebufferSize();
        if (framebufferWidth > 0 && framebufferHeight > 0 && ctx.editor->resizeWindowForLayout())
            std::tie(framebufferWidth, framebufferHeight) = window.framebufferSize();
        if (framebufferWidth == 0 || framebufferHeight == 0)
        {
            Window::waitEvents();
            deltaTimer.reset();
            continue;
        }

        const float deltaTime = static_cast<float>(deltaTimer.restartMilliseconds() / 1000.0);

        const auto& surfaceConfig = *ctx.renderer->getSurface()->getConfig();
        if (swapchainNeedsRebuild || surfaceConfig.width != uint32_t(framebufferWidth) ||
            surfaceConfig.height != uint32_t(framebufferHeight))
        {
            ctx.renderer->resizeSurface();
            ctx.editor->refreshUi();
            swapchainNeedsRebuild = false;
        }

        ctx.editor->applySceneSwitch();

        ctx.editor->tick(deltaTime);
        
        const auto editorFrame = ctx.editor->buildFrame();
        
        const auto result = ctx.renderer->render(
            ctx.sceneManager->scene(),
            ctx.sceneManager->sceneGeneration(),
            editorFrame.viewport,
            editorFrame.recordUi,
            deltaTime
        );
        
        swapchainNeedsRebuild = result == RenderFrameResult::SwapchainNeedsRebuild;

        Window::pollEvents();
    }
}

void Engine::shutdown() noexcept
{
    Context& ctx = context();

    if (const auto& sceneId = sceneManager->scene().id; !sceneId.empty())
        ctx.config.initialScene = sceneId;

    renderer->waitIdle();
    shutdownSubsystem(editor, ctx.editor);
    shutdownSubsystem(renderer, ctx.renderer);
    shutdownSubsystem(sceneManager, ctx.sceneManager);
    shutdownSubsystem(assetManager, ctx.assetManager);

    window.shutdown();
    ctx.window = nullptr;

    asset_json::save(kConfigFile, ctx.config);
}
