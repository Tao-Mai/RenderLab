#include "editor/Editor.h"

#include "asset/Asset.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/Renderer.h"
#include "render/device/VulkanContext.h"
#include "render/device/Swapchain.h"
#include "scene/SceneManager.h"
#include "scene/component/TransformComponent.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include <glm/glm.hpp>

namespace
{
constexpr std::string_view kSceneObjectsEnabled = "SceneObjects:enabled";
}

Editor::~Editor() { DCHECK(!inited); }

void Editor::init()
{
    DCHECK(context().window);
    DCHECK(context().renderer);

    editorCamera    = std::make_unique<EditorCamera>(context().sceneManager->scene().editorCamera);
    sceneGeneration = context().sceneManager->sceneGeneration();
    initBackend();
    DEBUG_EXEC(inited = true);
}

void Editor::initBackend()
{
    VulkanContext& vulkan    = context().renderer->vulkanContext();
    Swapchain&     swapchain = context().renderer->swapchainHandle();
    backend.init(
        context().window->nativeHandle(),
        *vulkan.instanceHandle(),
        *vulkan.physicalDeviceHandle(),
        *vulkan.deviceHandle(),
        vulkan.graphicsQueueFamilyIndex(),
        *vulkan.queueHandle(),
        static_cast<VkFormat>(swapchain.surfaceFormat().format),
        swapchain.minImageCount(),
        swapchain.imageCount()
    );
}

void Editor::shutdown() noexcept
{
    if (windowHasObjectList)
    {
        auto& config = *context().config;
        const auto& size = config.editorConfig();
        config.setEditorSize(std::max(size.width - ui.objectListInset(size.width), 1), size.height);
        windowHasObjectList = false;
    }
    editorCamera->resetNavigation();
    editorCamera.reset();
    ui.resetLayout();
    backend.shutdown();
    selectedId           = 0;
    dirty                = false;
    pendingSceneId.reset();
    pendingObjectActions = {};
    DEBUG_EXEC(inited = false);
}

void Editor::refreshUi()
{
    backend.shutdown();
    ui.resetLayout();
    initBackend();
}

bool Editor::resizeWindowForLayout()
{
    Context& ctx = context();
    const bool showObjectList = ctx.config->getSettings().getOption(kSceneObjectsEnabled, false);
    if (showObjectList == windowHasObjectList)
        return false;

    auto& window = *ctx.window;
    const auto [width, height] = window.size();
    const int inset = ui.objectListInset(width);
    const int nextWidth = std::max(width + (showObjectList ? inset : -inset), 1);
    window.resizeCentered(nextWidth, height);
    windowHasObjectList = showObjectList;
    return true;
}

void Editor::saveScene()
{
    Context& ctx = context();
    DCHECK(ctx.sceneManager);
    ctx.sceneManager->save();
    dirty = false;
}

void Editor::applySceneActions()
{
    if (!pendingSceneId && !pendingObjectActions.createType && !pendingObjectActions.deleteId)
        return;

    auto& scene = *context().sceneManager;

    if (pendingSceneId)
    {
        scene.load(*pendingSceneId);
        selectedId           = 0;
        dirty                = false;
        pendingSceneId.reset();
        pendingObjectActions = {};
        return;
    }

    if (pendingObjectActions.deleteId)
    {
        scene.deleteActor(pendingObjectActions.deleteId);
        if (selectedId == pendingObjectActions.deleteId)
            selectedId = 0;
    }
    if (pendingObjectActions.createType)
        selectedId = scene.createActor(*pendingObjectActions.createType).selectionId();

    pendingObjectActions = {};
    dirty                = true;
}

Editor::Frame
Editor::buildFrame(const EditorCamera& camera, float deltaTime, uint32_t swapchainWidth, uint32_t swapchainHeight)
{
    backend.beginFrame();
    ui.beginFrame(windowHasObjectList);
    auto& scene = context().sceneManager->scene();
    pendingObjectActions = ui.drawHierarchy(scene, selectedId);
    if (pendingObjectActions.renamed)
        dirty = true;

    const glm::mat4 view                 = camera.viewMatrix();
    const glm::mat4 projection           = camera.projectionMatrix(ui.sceneAspectRatio());
    const bool      enableGizmoShortcuts = !camera.isNavigationActive();
    auto*           selectedActor        = context().sceneManager->findActor(selectedId);

    if (!camera.isFreeMovementActive() && selectedActor != nullptr)
    {
        glm::mat4 gizmoProjection = projection;
        gizmoProjection[1][1] *= -1.0f;
        if (ui.drawGizmo(selectedActor->transform(), view, gizmoProjection, enableGizmoShortcuts))
        {
            dirty = true;
        }
    }

    EditorUI::InspectorResult inspector = ui.drawInspector(scene, selectedActor, dirty);
    pendingSceneId = std::move(inspector.sceneId);
    if (inspector.edited)
        dirty = true;
    if (!pendingSceneId && ui.saveRequested())
        saveScene();

    Frame      frame;
    auto&      input = frame.input;
    input.recordUi = [this](VkCommandBuffer commandBuffer) { backend.render(commandBuffer); };

    const auto& config = *context().config;
    const auto& renderer = *context().renderer;
    const auto* graph = renderer.getActiveGraph();
    if (config.editorConfig().objectPicking && graph && graph->getName() == config.renderGraphName() &&
        renderer.supportsObjectPicking() && !camera.isNavigationActive())
    {
        glm::vec2 clickNdc;
        if (ui.sceneClicked(clickNdc))
        {
            const ViewportRect viewport    = ui.sceneViewportPixels();
            const float        normalizedX = std::clamp(clickNdc.x * 0.5f + 0.5f, 0.0f, 1.0f);
            const float        normalizedY = std::clamp(clickNdc.y * 0.5f + 0.5f, 0.0f, 1.0f);
            input.pickX                    = std::min(
                static_cast<uint32_t>(std::max(viewport.x + normalizedX * viewport.width, 0.0f)),
                swapchainWidth - 1
            );
            input.pickY = std::min(
                static_cast<uint32_t>(std::max(viewport.y + normalizedY * viewport.height, 0.0f)),
                swapchainHeight - 1
            );
            input.requestPick = true;
        }
    }

    frame.view = camera.renderView(ui.sceneViewportPixels());
    ui.endFrame();
    return frame;
}

void Editor::applyPickResult(const RenderFrameResult& result)
{
    if (!result.hasPickResult)
    {
        return;
    }

    selectedId = result.pickedSelectionId;
}

bool Editor::wantsInput() const { return ui.wantsInput(); }

void Editor::tick(float deltaTime)
{
    const auto generation = context().sceneManager->sceneGeneration();
    if (generation != sceneGeneration)
    {
        editorCamera->resetNavigation();
        selectedId      = 0;
        dirty           = false;
        sceneGeneration = generation;
    }
    editorCamera->setInputEnabled(editorCamera->isNavigationActive() || !wantsInput());
    editorCamera->tick(deltaTime);
}
