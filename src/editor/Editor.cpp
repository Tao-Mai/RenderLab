#include "editor/Editor.h"

#include "asset/AssetDesc.h"
#include "asset/AssetDescManager.h"
#include "Camera.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "ecs/Transform.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "render/scene/GpuScene.h"
#include "core/Window.h"

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
        vulkan.graphicsQueueFamilyIndex(),
        *vulkan.queueHandle(),
        static_cast<VkFormat>(swapchain.surfaceFormat().format),
        swapchain.minImageCount(),
        swapchain.imageCount());
}

void Editor::shutdown() noexcept
{
    ui.shutdown();
    selectedObject = nullptr;
    dirty = false;
}

void Editor::refreshUi()
{
    ui.shutdown();
    init();
}

void Editor::saveScene()
{
    Context& ctx = context();
    CHECK(ctx.scene != nullptr, "scene must exist before save");
    CHECK(ctx.assetDescManager != nullptr, "assets must exist before save");
    CHECK(ctx.camera != nullptr, "camera must exist before save");

    ctx.scene->camera = ctx.camera->component();
    ctx.assetDescManager->save(*ctx.scene);
    dirty = false;
}

EditorFrameInput Editor::buildFrame(
    const Camera& camera,
    float deltaTime,
    uint32_t swapchainWidth,
    uint32_t swapchainHeight)
{
    ui.beginFrame(deltaTime);

    const ecs::Camera& currentCamera = camera.component();
    const ecs::Camera& savedCamera = context().scene->camera;
    dirty |= glm::any(glm::notEqual(currentCamera.position, savedCamera.position)) ||
        currentCamera.yaw != savedCamera.yaw ||
        currentCamera.pitch != savedCamera.pitch ||
        currentCamera.fieldOfView != savedCamera.fieldOfView;

    if (ui.saveRequested())
    {
        saveScene();
    }

    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 projection = camera.projectionMatrix(ui.sceneAspectRatio());
    const bool enableGizmoShortcuts = !camera.isNavigationActive();

    if (selectedObject != nullptr)
    {
        if (const auto transformIt = selectedObject->components.find("Transform");
            transformIt != selectedObject->components.end())
        {
            glm::mat4 gizmoProjection = projection;
            gizmoProjection[1][1] *= -1.0f;
            if (ui.drawGizmo(
                    *transformIt->second.try_cast<ecs::Transform>(),
                    view,
                    gizmoProjection,
                    enableGizmoShortcuts))
            {
                dirty = true;
            }
        }
    }

    EditorFrameInput input{
        .viewport = ui.sceneViewportPixels(),
        .aspectRatio = ui.sceneAspectRatio(),
        .selectedObject = selectedObject,
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

    const AssetId& sceneId = context().scene != nullptr ? context().scene->id : AssetId{};
    if (ui.drawInspector(selectedObject, sceneId, dirty))
    {
        dirty = true;
    }
    ui.endFrame();
    return input;
}

void Editor::applyPickResult(const EditorFrameResult& result, const GpuScene& scene)
{
    if (!result.hasPickResult)
    {
        return;
    }

    selectedObject = result.pickedSelectionId == 0
        ? nullptr
        : scene.findObject(result.pickedSelectionId);
}

bool Editor::wantsInput() const
{
    return ui.wantsInput();
}
