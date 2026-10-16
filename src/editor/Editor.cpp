#include "editor/Editor.h"

#include "asset/Asset.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "scene/SceneManager.h"
#include "scene/component/TransformComponent.h"

#include <algorithm>

#include <glm/glm.hpp>

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
    editorCamera->resetNavigation();
    editorCamera.reset();
    ui.shutdown();
    backend.shutdown();
    lightMarkers.reset();
    selectedId          = 0;
    dirty               = false;
    pendingSceneActions = {};
    DEBUG_EXEC(inited = false);
}

void Editor::refreshUi()
{
    backend.shutdown();
    ui.resetLayout();
    initBackend();
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
    if (!pendingSceneActions.sceneId && !pendingSceneActions.createType && !pendingSceneActions.deleteId)
        return;

    auto& scene = *context().sceneManager;

    if (pendingSceneActions.sceneId)
    {
        scene.load(*pendingSceneActions.sceneId);
        selectedId          = 0;
        dirty               = false;
        pendingSceneActions = {};
        return;
    }

    if (pendingSceneActions.deleteId)
    {
        scene.deleteActor(pendingSceneActions.deleteId);
        if (selectedId == pendingSceneActions.deleteId)
            selectedId = 0;
    }
    if (pendingSceneActions.createType)
        selectedId = scene.createActor(*pendingSceneActions.createType).selectionId();

    pendingSceneActions = {};
    dirty               = true;
}

Editor::Frame
Editor::buildFrame(const EditorCamera& camera, float deltaTime, uint32_t swapchainWidth, uint32_t swapchainHeight)
{
    backend.beginFrame();
    ui.beginFrame(deltaTime);
    pendingSceneActions = ui.drawHierarchy(context().sceneManager->scene(), selectedId);
    if (pendingSceneActions.renamed)
        dirty = true;

    if (!pendingSceneActions.sceneId && ui.saveRequested())
    {
        saveScene();
    }

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

    Frame frame;
    auto& input    = frame.input;
    input.recordUi = [this](VkCommandBuffer commandBuffer) { return backend.render(commandBuffer); };

    if (!camera.isNavigationActive())
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

    EditorUI::InspectorResult inspector = ui.drawInspector(context().sceneManager->scene(), selectedActor, dirty);
    if (inspector.edited)
    {
        dirty = true;
    }
    frame.view = camera.renderView(ui.sceneViewportPixels());
    lightMarkers.prepare(context().sceneManager->scene(), *renderContext().resources);
    input.lightDrawItems = lightMarkers.drawItems();
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
