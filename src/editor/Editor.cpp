#include "editor/Editor.h"

#include "core/Context.h"
#include "core/Window.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"

#include <algorithm>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <imm.h>
#endif

Editor::~Editor() = default;

void Editor::init()
{
    editorCamera = std::make_unique<EditorCamera>(context().sceneManager->scene().camera);

    auto* handle = context().window->nativeHandle();
    glfwSetKeyCallback(handle, [](GLFWwindow*, int key, int, int action, int modifiers)
    {
        if (action != GLFW_PRESS || modifiers != 0)
            return;

        if (key == GLFW_KEY_ESCAPE)
            context().window->requestClose();
        else if (key == GLFW_KEY_V)
            context().editor->camera().requestFreeMovementToggle();
    });
    glfwSetScrollCallback(handle, [](GLFWwindow*, double, double yOffset)
    {
        context().editor->camera().addScrollOffset(yOffset);
    });

    initBackend();

#ifdef _WIN32
    if (HWND hwnd = glfwGetWin32Window(handle))
        previousImeContext = ImmAssociateContext(hwnd, nullptr);
#endif
}

void Editor::initBackend()
{
    backend.init(
        context().window->nativeHandle(),
        context().renderer->getDevice(),
        context().renderer->getShaderManager(),
        context().renderer->getSurface()->getConfig()->format
    );
}

void Editor::shutdown() noexcept
{
    if (windowHasObjectList)
    {
        auto& settings = context().config.editor;
        settings.width = std::max(settings.width - ui.objectListInset(settings.width), 1);
        windowHasObjectList = false;
    }
    editorCamera->resetNavigation();
    ui.resetLayout();
    backend.shutdown();

    auto* handle = context().window->nativeHandle();
    glfwSetKeyCallback(handle, nullptr);
    glfwSetScrollCallback(handle, nullptr);
    editorCamera.reset();
    pendingSceneId.reset();

#ifdef _WIN32
    if (previousImeContext)
    {
        if (HWND hwnd = glfwGetWin32Window(handle))
            ImmAssociateContext(hwnd, static_cast<HIMC>(previousImeContext));
        previousImeContext = nullptr;
    }
#endif
}

void Editor::refreshUi()
{
    backend.shutdown();
    ui.resetLayout();
    initBackend();
}

bool Editor::resizeWindowForLayout()
{
    Context&   ctx            = context();
    const bool showObjectList = ctx.config.editor.showSceneObjects;
    if (showObjectList == windowHasObjectList)
        return false;

    const auto& window = *ctx.window;
    const auto [width, height] = window.size();
    const int inset = ui.objectListInset(width);
    const int nextWidth = std::max(width + (showObjectList ? inset : -inset), 1);

    window.resizeCentered(nextWidth, height);
    windowHasObjectList = showObjectList;

    return true;
}

void Editor::applySceneSwitch()
{
    if (!pendingSceneId)
        return;

    context().sceneManager->load(*pendingSceneId);
    editorCamera->resetNavigation();
    pendingSceneId.reset();
}

Editor::Frame Editor::buildFrame()
{
    backend.beginFrame();
    ui.beginFrame(windowHasObjectList);

    const auto& scene = context().sceneManager->scene();
    ui.drawSceneList(scene);
    pendingSceneId = ui.drawControls(scene);
    ui.endFrame();

    return {
        .viewport = ui.sceneViewportPixels(),
        .recordUi = [this](rhi::IRenderPassEncoder& commands, uint32_t frameIndex)
        {
            backend.render(commands, frameIndex);
        },
    };
}

void Editor::tick(float deltaTime)
{
    editorCamera->setInputEnabled(editorCamera->isNavigationActive() || !ui.wantsInput());
    editorCamera->tick(deltaTime);
}
