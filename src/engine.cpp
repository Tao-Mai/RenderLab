#include "engine.h"

#include "scene/light.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <glm/glm.hpp>

Engine::~Engine()
{
    shutdown();
}

void Engine::initialize()
{
    if (initialized)
    {
        return;
    }

    try
    {
        window.initialize(1440, 900, "RenderLab");
        inputMethod.activateEnglish();
        scene = Scene::load(assets.path("scenes/default.scene.json"));
        camera.configure(scene.camera);
        renderer.initialize(window);
        renderer.loadScene(scene, assets);
        initializeEditor();
        initialized = true;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

void Engine::run()
{
    if (!initialized)
    {
        throw std::logic_error("Engine must be initialized before run()");
    }

    mainLoop();
}

void Engine::initializeEditor()
{
    auto&       vulkan   = renderer.vulkanContext();
    auto&       swapchain = renderer.swapchainHandle();
    editorUI.initialize(
        window.nativeHandle(),
        *vulkan.instanceHandle(),
        *vulkan.physicalDeviceHandle(),
        *vulkan.deviceHandle(),
        vulkan.queueFamilyIndex(),
        *vulkan.queueHandle(),
        static_cast<VkFormat>(swapchain.surfaceFormat().format),
        swapchain.minImageCount(),
        swapchain.imageCount());
}

EditorFrameInput Engine::buildEditorFrame(const Camera& currentCamera, float deltaTime)
{
    editorUI.beginFrame(deltaTime);

    const glm::mat4 view       = currentCamera.viewMatrix();
    const glm::mat4 projection = currentCamera.projectionMatrix(editorUI.sceneAspectRatio());

    if (selectedObject != nullptr)
    {
        glm::mat4 gizmoProjection = projection;
        gizmoProjection[1][1] *= -1.0f;
        editorUI.drawGizmo(
            selectedObject->transform,
            view,
            gizmoProjection,
            !currentCamera.isNavigationActive());
    }
    else if (selectedLight != nullptr)
    {
        glm::mat4 gizmoProjection = projection;
        gizmoProjection[1][1] *= -1.0f;
        editorUI.drawLightGizmo(
            *selectedLight,
            view,
            gizmoProjection,
            !currentCamera.isNavigationActive());
    }

    EditorFrameInput input{
        .viewport = editorUI.sceneViewportPixels(),
        .aspectRatio = editorUI.sceneAspectRatio(),
        .selectedObject = selectedObject,
        .selectedLight = selectedLight,
        .recordUi = [this](VkCommandBuffer commandBuffer) {
            editorUI.render(commandBuffer);
        },
    };

    if (!currentCamera.isNavigationActive())
    {
        glm::vec2 clickNdc;
        if (editorUI.sceneClicked(clickNdc))
        {
            const ViewportRect viewport    = input.viewport;
            const float        normalizedX = std::clamp(clickNdc.x * 0.5f + 0.5f, 0.0f, 1.0f);
            const float        normalizedY = std::clamp(clickNdc.y * 0.5f + 0.5f, 0.0f, 1.0f);
            const auto         extent      = renderer.swapchainHandle().extent();
            input.pickX                    = std::min(
                static_cast<uint32_t>(
                    std::max(viewport.x + normalizedX * viewport.width, 0.0f)),
                extent.width - 1);
            input.pickY = std::min(
                static_cast<uint32_t>(
                    std::max(viewport.y + normalizedY * viewport.height, 0.0f)),
                extent.height - 1);
            input.requestPick = true;
        }
    }

    editorUI.drawInspector(selectedObject, selectedLight);
    editorUI.endFrame();
    return input;
}

void Engine::applyPickResult(const EditorFrameResult& result)
{
    if (!result.hasPickResult)
    {
        return;
    }

    selectedObject = renderer.gpuScene().findObject(result.pickedSelectionId);
    selectedLight  = renderer.gpuScene().findLight(result.pickedSelectionId);
    if (result.pickedSelectionId == 0)
    {
        selectedObject = nullptr;
        selectedLight  = nullptr;
    }
}

void Engine::mainLoop()
{
    auto previousTime = std::chrono::steady_clock::now();
    while (!window.shouldClose())
    {
        window.pollEvents();
        if (window.keyPressed(GLFW_KEY_ESCAPE))
        {
            window.requestClose();
            break;
        }
        const auto  currentTime = std::chrono::steady_clock::now();
        const float deltaTime   = std::chrono::duration<float>(
            currentTime - previousTime).count();
        previousTime = currentTime;

        camera.update(
            window,
            deltaTime,
            camera.isNavigationActive() || !editorUI.wantsInput());

        const EditorFrameInput  editorInput  = buildEditorFrame(camera, deltaTime);
        const EditorFrameResult editorResult = renderer.render(camera, editorInput);
        applyPickResult(editorResult);
    }

    renderer.waitIdle();
}

void Engine::shutdown() noexcept
{
    editorUI.shutdown();
    renderer.shutdown();
    assets.clear();
    inputMethod.restore();
    window.shutdown();
    selectedObject = nullptr;
    selectedLight  = nullptr;
    initialized    = false;
}
