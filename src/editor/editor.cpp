#include "editor/editor.h"

#include "camera.h"
#include "core/context.h"
#include "logger.h"
#include "render/device/vulkan_context.h"
#include "render/present/swapchain.h"
#include "render/scene/gpu_scene.h"
#include "window.h"

#include <algorithm>

#include <glm/glm.hpp>

void Editor::init()
{
    CHECK(context().window != nullptr, "Window must exist before Editor");
    CHECK(context().renderer != nullptr, "Renderer must exist before Editor");

    VulkanContext& vulkan = context().renderer->vulkanContext();
    Swapchain& swapchain = context().renderer->swapchainHandle();
    ui.init(
        context().window->nativeHandle(),
        *vulkan.instanceHandle(),
        *vulkan.physicalDeviceHandle(),
        *vulkan.deviceHandle(),
        vulkan.queueFamilyIndex(),
        *vulkan.queueHandle(),
        static_cast<VkFormat>(swapchain.surfaceFormat().format),
        swapchain.minImageCount(),
        swapchain.imageCount());
}

void Editor::shutdown() noexcept
{
    ui.shutdown();
    selectedObject = nullptr;
    selectedLight = nullptr;
}

EditorFrameInput Editor::buildFrame(
    const Camera& camera,
    float deltaTime,
    uint32_t swapchainWidth,
    uint32_t swapchainHeight)
{
    ui.beginFrame(deltaTime);

    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 projection = camera.projectionMatrix(ui.sceneAspectRatio());
    const bool enableGizmoShortcuts = !camera.isNavigationActive();

    if (selectedObject != nullptr)
    {
        glm::mat4 gizmoProjection = projection;
        gizmoProjection[1][1] *= -1.0f;
        ui.drawGizmo(selectedObject->transform, view, gizmoProjection, enableGizmoShortcuts);
    }
    else if (selectedLight != nullptr)
    {
        glm::mat4 gizmoProjection = projection;
        gizmoProjection[1][1] *= -1.0f;
        ui.drawLightGizmo(*selectedLight, view, gizmoProjection, enableGizmoShortcuts);
    }

    EditorFrameInput input{
        .viewport = ui.sceneViewportPixels(),
        .aspectRatio = ui.sceneAspectRatio(),
        .selectedObject = selectedObject,
        .selectedLight = selectedLight,
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

    ui.drawInspector(selectedObject, selectedLight);
    ui.endFrame();
    return input;
}

void Editor::applyPickResult(const EditorFrameResult& result, const GpuScene& scene)
{
    if (!result.hasPickResult)
    {
        return;
    }

    selectedObject = scene.findObject(result.pickedSelectionId);
    selectedLight = scene.findLight(result.pickedSelectionId);
    if (result.pickedSelectionId == 0)
    {
        selectedObject = nullptr;
        selectedLight = nullptr;
    }
}

bool Editor::wantsInput() const
{
    return ui.wantsInput();
}
