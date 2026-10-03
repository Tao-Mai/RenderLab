#include "editor/Editor.h"

#include "asset/Asset.h"
#include "scene/SceneManager.h"
#include "scene/component/CameraComponent.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "scene/component/TransformComponent.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "core/Window.h"

#include <algorithm>

#include <glm/glm.hpp>

void Editor::init()
{
    DCHECK(context().window);
    DCHECK(context().renderer);

    VulkanContext& vulkan = context().renderer->vulkanContext();
    Swapchain& swapchain = context().renderer->swapchainHandle();
    ui.init(
        context().window->nativeHandle(),
        *vulkan.instanceHandle(),
        *vulkan.physicalDeviceHandle(),
        *vulkan.deviceHandle(),
        vulkan.graphicsQueueFamilyIndex(),
        *vulkan.queueHandle(),
        static_cast<VkFormat>(swapchain.surfaceFormat().format),
        swapchain.minImageCount(),
        swapchain.imageCount());
}

void Editor::shutdown() noexcept
{
    ui.shutdown();
    selectedId = 0;
    dirty = false;
}

void Editor::refreshUi()
{
    ui.shutdown(false);
    init();
}

void Editor::saveScene()
{
    Context& ctx = context();
    DCHECK(ctx.sceneManager);
    ctx.sceneManager->save();
    dirty = false;
}

EditorFrameInput Editor::buildFrame(
    const CameraComponent& camera,
    float deltaTime,
    uint32_t swapchainWidth,
    uint32_t swapchainHeight)
{
    ui.beginFrame(deltaTime);

    if (ui.saveRequested())
    {
        saveScene();
    }

    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 projection = camera.projectionMatrix(ui.sceneAspectRatio());
    const bool enableGizmoShortcuts = !camera.isNavigationActive();
    auto* selectedActor = context().sceneManager->findActor(selectedId);

    if (!camera.isFreeMovementActive() && selectedActor != nullptr)
    {
        glm::mat4 gizmoProjection = projection;
        gizmoProjection[1][1] *= -1.0f;
        if (ui.drawGizmo(
                selectedActor->transform(),
                view,
                gizmoProjection,
                enableGizmoShortcuts))
        {
            dirty = true;
        }
    }

    EditorFrameInput input{
        .viewport = ui.sceneViewportPixels(),
        .aspectRatio = ui.sceneAspectRatio(),
        .selectedActor = selectedActor,
        .recordUi = [this](VkCommandBuffer commandBuffer) {
            ui.render(commandBuffer);
        },
    };

    if (!camera.isNavigationActive())
    {
        glm::vec2 clickNdc;
        if (ui.sceneClicked(clickNdc))
        {
            const ViewportRect viewport = input.viewport;
            const float normalizedX = std::clamp(clickNdc.x * 0.5f + 0.5f, 0.0f, 1.0f);
            const float normalizedY = std::clamp(clickNdc.y * 0.5f + 0.5f, 0.0f, 1.0f);
            input.pickX = std::min(
                static_cast<uint32_t>(std::max(viewport.x + normalizedX * viewport.width, 0.0f)),
                swapchainWidth - 1);
            input.pickY = std::min(
                static_cast<uint32_t>(std::max(viewport.y + normalizedY * viewport.height, 0.0f)),
                swapchainHeight - 1);
            input.requestPick = true;
        }
    }

    EditorUI::InspectorResult inspector = ui.drawInspector(
        context().sceneManager->scene(), selectedActor, dirty);
    if (inspector.edited)
    {
        dirty = true;
    }
    if (inspector.environmentChanged || inspector.resourcesChanged)
    {
        context().renderer->loadScene();
    }
    ui.endFrame();
    return input;
}

void Editor::applyPickResult(const EditorFrameResult& result)
{
    if (!result.hasPickResult)
    {
        return;
    }

    selectedId = result.pickedSelectionId;
}

bool Editor::wantsInput() const
{
    return ui.wantsInput();
}
